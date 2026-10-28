#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#define MMNOSOUND
#define CURL_STATICLIB
#endif
#include <curl/curl.h>
#include "ca_bundle.h"
#include <raylib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define BASE_URL "https://w10l-crb-backend.syedas666z-a94.workers.dev"

typedef enum { SCR_LOGIN, SCR_DASH, SCR_LOBBY, SCR_COURT } Screen;
typedef enum { TAG_CAP, TAG_AUTH, TAG_SEARCH, TAG_ADMIN, TAG_JOIN, TAG_STATE, TAG_ACT } Tag;

typedef struct { char *d; size_t n; } Buf;

static struct {
    CURLM *m;
    CURL *e;
    Buf body;
    bool busy;
    int tag;
    long http;
    char post[4096];
} net;

static size_t OnBody(char *p, size_t s, size_t n, void *u) {
    Buf *b = (Buf *)u;
    size_t k = s * n;
    char *nd = realloc(b->d, b->n + k + 1);
    if (!nd) return 0;
    b->d = nd;
    memcpy(b->d + b->n, p, k);
    b->n += k;
    b->d[b->n] = '\0';
    return k;
}

static void NetInit(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    net.m = curl_multi_init();
}

static bool NetBegin(int tag, const char *path, const char *post) {
    if (net.busy) return false;
    free(net.body.d);
    net.body.d = NULL;
    net.body.n = 0;
    net.tag = tag;
    net.http = 0;
    char url[512];
    snprintf(url, sizeof(url), "%s%s", BASE_URL, path);
    net.e = curl_easy_init();
    curl_easy_setopt(net.e, CURLOPT_URL, url);
    curl_easy_setopt(net.e, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(net.e, CURLOPT_WRITEDATA, &net.body);
    curl_easy_setopt(net.e, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(net.e, CURLOPT_NOSIGNAL, 1L);
struct curl_blob cablob = { (void *)CA_BUNDLE_DATA, CA_BUNDLE_LEN, CURL_BLOB_COPY };
curl_easy_setopt(net.e, CURLOPT_CAINFO_BLOB, &cablob);
    if (post) {
        snprintf(net.post, sizeof(net.post), "%s", post);
        curl_easy_setopt(net.e, CURLOPT_POSTFIELDS, net.post);
    }
    curl_multi_add_handle(net.m, net.e);
    net.busy = true;
    return true;
}

static bool NetPump(void) {
    if (!net.busy) return false;
    int running = 0;
    curl_multi_perform(net.m, &running);
    int left;
    CURLMsg *msg;
    while ((msg = curl_multi_info_read(net.m, &left))) {
        if (msg->msg == CURLMSG_DONE) {
            net.http = 0;
            if (msg->data.result == CURLE_OK) curl_easy_getinfo(net.e, CURLINFO_RESPONSE_CODE, &net.http);
            curl_multi_remove_handle(net.m, net.e);
            curl_easy_cleanup(net.e);
            net.e = NULL;
            net.busy = false;
            if (!net.body.d) net.body.d = calloc(1, 1);
            return true;
        }
    }
    return false;
}

static void AddField(char *buf, size_t size, const char *key, const char *val) {
    char *esc = curl_easy_escape(NULL, val, 0);
    size_t len = strlen(buf);
    snprintf(buf + len, size - len, "%s%s=%s", len ? "&" : "", key, esc ? esc : "");
    curl_free(esc);
}

static bool GetVal(const char *body, const char *key, char *out, int outSize) {
    size_t kl = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            const char *end = strchr(v, '\n');
            int vl = end ? (int)(end - v) : (int)strlen(v);
            char *tmp = malloc(vl + 1);
            memcpy(tmp, v, vl);
            tmp[vl] = '\0';
            int dl = 0;
            char *dec = curl_easy_unescape(NULL, tmp, 0, &dl);
            snprintf(out, outSize, "%s", dec ? dec : "");
            curl_free(dec);
            free(tmp);
            return true;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    out[0] = '\0';
    return false;
}

static int GetInt(const char *body, const char *key, int def) {
    char t[32];
    return GetVal(body, key, t, sizeof(t)) ? atoi(t) : def;
}

typedef struct { char u[32]; int role; int banned; int term; } SRow;
static SRow srows[10];
static int srowCount = 0;

static void ParseSearch(const char *b) {
    srowCount = 0;
    int n = GetInt(b, "count", 0);
    if (n > 10) n = 10;
    for (int i = 0; i < n; i++) {
        char k[8], v[160];
        snprintf(k, sizeof(k), "u%d", i);
        if (!GetVal(b, k, v, sizeof(v))) continue;
        char *p1 = strchr(v, ',');
        if (!p1) continue;
        *p1 = 0;
        char *p2 = strchr(p1 + 1, ',');
        if (!p2) continue;
        *p2 = 0;
        char *p3 = strchr(p2 + 1, ',');
        if (!p3) continue;
        *p3 = 0;
        snprintf(srows[srowCount].u, sizeof(srows[srowCount].u), "%s", v);
        srows[srowCount].role = atoi(p1 + 1);
        srows[srowCount].banned = atoi(p2 + 1);
        srows[srowCount].term = atoi(p3 + 1);
        srowCount++;
    }
}

typedef struct { char name[32]; char seat[12]; } Member;

typedef struct {
    char phase[8];
    char seat[12];
    int lidx;
    int players;
    Member mem[15];
    char status[12];
    char lname[2][32];
    int lsub[2];
    char lroast[2][512];
    int winner;
    int round;
} Game;

static void ParseState(Game *g, const char *b) {
    GetVal(b, "phase", g->phase, sizeof(g->phase));
    GetVal(b, "seat", g->seat, sizeof(g->seat));
    g->lidx = GetInt(b, "lawyeridx", -1);
    g->players = GetInt(b, "players", 0);
    if (g->players > 15) g->players = 15;
    for (int i = 0; i < g->players; i++) {
        char k[8], v[80];
        snprintf(k, sizeof(k), "p%d", i);
        if (GetVal(b, k, v, sizeof(v))) {
            char *c = strchr(v, ',');
            if (c) {
                *c = 0;
                snprintf(g->mem[i].seat, sizeof(g->mem[i].seat), "%s", c + 1);
            } else g->mem[i].seat[0] = 0;
            snprintf(g->mem[i].name, sizeof(g->mem[i].name), "%s", v);
        }
    }
    GetVal(b, "status", g->status, sizeof(g->status));
    GetVal(b, "lawyer0", g->lname[0], sizeof(g->lname[0]));
    GetVal(b, "lawyer1", g->lname[1], sizeof(g->lname[1]));
    g->lsub[0] = GetInt(b, "sub0", 0);
    g->lsub[1] = GetInt(b, "sub1", 0);
    GetVal(b, "roast0", g->lroast[0], sizeof(g->lroast[0]));
    GetVal(b, "roast1", g->lroast[1], sizeof(g->lroast[1]));
    g->winner = GetInt(b, "winner", -1);
    g->round = GetInt(b, "round", 1);
}

static bool ButtonHit(Rectangle r) {
    return CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawButton(Rectangle r, const char *text, bool enabled) {
    Color fill = (Color){80, 80, 80, 255};
    if (enabled) {
        fill = CheckCollisionPointRec(GetMousePosition(), r) ? (Color){220, 70, 70, 255} : (Color){160, 40, 40, 255};
    }
    DrawRectangleRounded(r, 0.2f, 8, fill);
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, (Color){20, 20, 20, 255});
    Vector2 size = MeasureTextEx(GetFontDefault(), text, 16, 1);
    DrawText(text, (int)(r.x + r.width / 2 - size.x / 2), (int)(r.y + r.height / 2 - size.y / 2), 16, WHITE);
}

static void UpdateTextInput(char *buffer, int bufferSize) {
    int key = GetCharPressed();
    while (key > 0) {
        int len = (int)strlen(buffer);
        if (key >= 32 && key < 127 && len < bufferSize - 1) {
            buffer[len] = (char)key;
            buffer[len + 1] = '\0';
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        int len = (int)strlen(buffer);
        if (len > 0) buffer[len - 1] = '\0';
    }
}

static void DrawInputBox(Rectangle r, const char *buffer, bool active) {
    DrawRectangleRounded(r, 0.1f, 6, (Color){30, 30, 40, 255});
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, active ? YELLOW : (Color){90, 90, 110, 255});
    DrawText(buffer, (int)(r.x + 10), (int)(r.y + r.height / 2 - 10), 20, WHITE);
}

static void DrawTextWrapped(const char *text, int x, int y, int maxWidth, int spacing, Color color) {
    int start = 0;
    int len = (int)strlen(text);
    int lineY = y;
    while (start < len) {
        int count = maxWidth / (spacing / 2 + 1);
        if (count <= 0) count = 1;
        if (start + count > len) count = len - start;
        char line[128];
        int copyLen = count > 127 ? 127 : count;
        strncpy(line, text + start, copyLen);
        line[copyLen] = '\0';
        DrawText(line, x, lineY, spacing, color);
        start += copyLen;
        lineY += spacing + 6;
    }
}

int main(void) {
    InitWindow(1280, 720, "W10L courtroom roast battle");
if (!IsWindowReady()) {
    FILE *f = fopen("W10L-CRB-error.txt", "w");
    if (f) { fprintf(f, "Window creation failed: no OpenGL 3.3 context available (VM without 3D acceleration, RDP session, or missing GPU drivers)\n"); fclose(f); }
    return 1;
}
    SetTargetFPS(60);
    NetInit();

    Screen screen = SCR_LOGIN;
    char username[32] = "";
    char password[64] = "";
    char capAnswer[8] = "";
    char capQuestion[64] = "";
    char capToken[128] = "";
    char token[96] = "";
    char myUsername[32] = "";
    int myRole = 0;
    char message[160] = "";
    char searchQuery[32] = "";
    char lobbyCode[8] = "";
    char inputRoast[512] = "";
    int focus = 0;
    bool wantCap = true;
    bool inLobby = false;
    double nextPoll = 0;
    Game g;
    memset(&g, 0, sizeof(g));
    g.lidx = -1;
    g.winner = -1;

    while (!WindowShouldClose()) {
        if (NetPump()) {
            const char *body = net.body.d;
            char err[128];
            bool haveErr = GetVal(body, "err", err, sizeof(err));
            if (net.tag == TAG_CAP) {
                if (net.http == 200 && net.body.n > 0) {
                    GetVal(body, "question", capQuestion, sizeof(capQuestion));
                    GetVal(body, "token", capToken, sizeof(capToken));
                    capAnswer[0] = '\0';
                } else snprintf(message, sizeof(message), "Captcha fetch failed");
            } else if (net.tag == TAG_AUTH) {
                if (net.http == 200 && GetVal(body, "token", token, sizeof(token))) {
                    GetVal(body, "username", myUsername, sizeof(myUsername));
                    myRole = GetInt(body, "role", 0);
                    message[0] = '\0';
                    searchQuery[0] = '\0';
                    srowCount = 0;
                    wantCap = false;
                    focus = 0;
                    screen = SCR_DASH;
                } else {
                    snprintf(message, sizeof(message), "%s", haveErr ? err : (net.http ? "Server error" : "Connection failed"));
                    wantCap = true;
                }
            } else if (net.tag == TAG_SEARCH) {
                if (net.http == 200) ParseSearch(body);
                else snprintf(message, sizeof(message), "%s", haveErr ? err : "Search failed");
            } else if (net.tag == TAG_ADMIN) {
                if (net.http == 200) {
                    message[0] = '\0';
                    if (searchQuery[0] && !net.busy) {
                        char q[160] = "", path[256];
                        AddField(q, sizeof(q), "query", searchQuery);
                        AddField(q, sizeof(q), "token", token);
                        snprintf(path, sizeof(path), "/admin/search?%s", q);
                        NetBegin(TAG_SEARCH, path, NULL);
                    }
                } else snprintf(message, sizeof(message), "%s", haveErr ? err : "Action failed");
            } else if (net.tag == TAG_JOIN) {
                if (net.http == 200 && GetVal(body, "code", lobbyCode, sizeof(lobbyCode))) {
                    inLobby = true;
                    memset(&g, 0, sizeof(g));
                    g.lidx = -1;
                    g.winner = -1;
                    message[0] = '\0';
                    nextPoll = 0;
                    screen = SCR_LOBBY;
                } else {
                    snprintf(message, sizeof(message), "%s", haveErr ? err : "Lobby failed");
                    inLobby = false;
                    lobbyCode[0] = '\0';
                }
            } else if (net.tag == TAG_STATE) {
                if (net.http == 200) {
                    ParseState(&g, body);
                    if (strcmp(g.phase, "court") == 0 && screen == SCR_LOBBY) screen = SCR_COURT;
                    if (strcmp(g.phase, "lobby") == 0 && screen == SCR_COURT) screen = SCR_LOBBY;
                } else {
                    inLobby = false;
                    lobbyCode[0] = '\0';
                    screen = SCR_DASH;
                    snprintf(message, sizeof(message), "%s", haveErr ? err : "Lobby closed");
                }
                nextPoll = GetTime() + 1.0;
            } else if (net.tag == TAG_ACT) {
                nextPoll = 0;
            }
        }

        if (!net.busy) {
            if (wantCap && screen == SCR_LOGIN) {
                if (NetBegin(TAG_CAP, "/captcha", NULL)) wantCap = false;
            } else if (inLobby && (screen == SCR_LOBBY || screen == SCR_COURT) && GetTime() >= nextPoll) {
                char q[160] = "", path[256];
                AddField(q, sizeof(q), "code", lobbyCode);
                AddField(q, sizeof(q), "token", token);
                snprintf(path, sizeof(path), "/lobby/state?%s", q);
                NetBegin(TAG_STATE, path, NULL);
            }
        }

        BeginDrawing();
        ClearBackground((Color){18, 18, 24, 255});

        if (screen == SCR_LOGIN) {
            Rectangle nameBox = {440, 200, 400, 44};
            Rectangle passBox = {440, 270, 400, 44};
            Rectangle capBox = {440, 360, 400, 44};
            Rectangle loginBtn = {440, 430, 195, 50};
            Rectangle regBtn = {645, 430, 195, 50};

            if (ButtonHit(nameBox)) focus = 0;
            if (ButtonHit(passBox)) focus = 1;
            if (ButtonHit(capBox)) focus = 2;
            if (IsKeyPressed(KEY_TAB)) focus = (focus + 1) % 3;

            if (focus == 0) UpdateTextInput(username, sizeof(username));
            else if (focus == 1) UpdateTextInput(password, sizeof(password));
            else UpdateTextInput(capAnswer, sizeof(capAnswer));

            DrawText("W10L courtroom roast battle", 370, 90, 40, RED);
            DrawText("Login / Register", 520, 150, 20, GRAY);
            DrawText("Username", 440, 175, 20, WHITE);
            DrawText("Password", 440, 245, 20, WHITE);
            DrawText("Captcha Answer", 440, 315, 20, WHITE);
            DrawText(capQuestion[0] ? capQuestion : "loading...", 440, 338, 16, YELLOW);

            DrawInputBox(nameBox, username, focus == 0);
            DrawInputBox(passBox, password, focus == 1);
            DrawInputBox(capBox, capAnswer, focus == 2);

            bool ready = strlen(username) > 0 && strlen(password) > 0 && strlen(capAnswer) > 0 && capQuestion[0] != '\0' && !net.busy;
            DrawButton(loginBtn, "Login", ready);
            DrawButton(regBtn, "Register", ready);
            DrawText(message, 440, 500, 20, YELLOW);

            if (ready && ButtonHit(loginBtn)) {
                char post[512] = "";
                AddField(post, sizeof(post), "username", username);
                AddField(post, sizeof(post), "password", password);
                AddField(post, sizeof(post), "captchaAnswer", capAnswer);
                AddField(post, sizeof(post), "captchaToken", capToken);
                NetBegin(TAG_AUTH, "/auth/login", post);
            }
            if (ready && ButtonHit(regBtn)) {
                char post[512] = "";
                AddField(post, sizeof(post), "username", username);
                AddField(post, sizeof(post), "password", password);
                AddField(post, sizeof(post), "captchaAnswer", capAnswer);
                AddField(post, sizeof(post), "captchaToken", capToken);
                NetBegin(TAG_AUTH, "/auth/register", post);
            }
        } else if (screen == SCR_DASH) {
            DrawText(TextFormat("Welcome, %s (%s)", myUsername, myRole == 2 ? "Owner" : (myRole == 1 ? "Mod" : "User")), 40, 30, 30, RED);

            Rectangle searchBox = {40, 90, 300, 44};
            Rectangle searchBtn = {350, 90, 100, 44};
            Rectangle playBtn = {1000, 90, 240, 50};
            Rectangle logoutBtn = {1000, 150, 240, 44};

            if (ButtonHit(searchBox)) focus = 0;
            if (focus == 0) UpdateTextInput(searchQuery, sizeof(searchQuery));

            DrawInputBox(searchBox, searchQuery, focus == 0);
            DrawButton(searchBtn, "Search", strlen(searchQuery) > 0 && !net.busy);
            DrawButton(playBtn, "Play Game", true);
            DrawButton(logoutBtn, "Logout", true);
            DrawText(message, 40, 660, 20, YELLOW);

            if ((ButtonHit(searchBtn) || (IsKeyPressed(KEY_ENTER) && focus == 0)) && strlen(searchQuery) > 0 && !net.busy) {
                char q[160] = "", path[256];
                AddField(q, sizeof(q), "query", searchQuery);
                AddField(q, sizeof(q), "token", token);
                snprintf(path, sizeof(path), "/admin/search?%s", q);
                NetBegin(TAG_SEARCH, path, NULL);
            }

            if (ButtonHit(playBtn)) {
                screen = SCR_LOBBY;
                inLobby = false;
                lobbyCode[0] = '\0';
                memset(&g, 0, sizeof(g));
                g.lidx = -1;
                g.winner = -1;
                message[0] = '\0';
                focus = 0;
            }

            if (ButtonHit(logoutBtn)) {
                screen = SCR_LOGIN;
                token[0] = '\0';
                username[0] = '\0';
                password[0] = '\0';
                myUsername[0] = '\0';
                myRole = 0;
                inLobby = false;
                lobbyCode[0] = '\0';
                wantCap = true;
                focus = 0;
            }

            for (int i = 0; i < srowCount; i++) {
                int y = 170 + i * 48;
                DrawText(srows[i].u, 60, y, 20, WHITE);
                DrawText(srows[i].role == 2 ? "Owner" : (srows[i].role == 1 ? "Mod" : "User"), 220, y, 20, GRAY);
                if (srows[i].banned) DrawText("BANNED", 300, y, 20, RED);
                if (srows[i].term) DrawText("TERMINATED", 390, y, 20, RED);

                bool isSelf = strcmp(srows[i].u, myUsername) == 0;
                bool targetPlain = srows[i].role == 0;
                bool canBanTerm = !isSelf && (myRole == 2 || (myRole == 1 && targetPlain));
                bool canMod = !isSelf && myRole == 2;

                Rectangle banBtn = {600, y - 5, 90, 30};
                Rectangle modBtn = {700, y - 5, 110, 30};
                Rectangle termBtn = {820, y - 5, 100, 30};

                if (canBanTerm) {
                    DrawButton(banBtn, srows[i].banned ? "Unban" : "Ban", !net.busy);
                    if (ButtonHit(banBtn) && !net.busy) {
                        char post[256] = "";
                        AddField(post, sizeof(post), "targetUsername", srows[i].u);
                        AddField(post, sizeof(post), "banned", srows[i].banned ? "false" : "true");
                        AddField(post, sizeof(post), "token", token);
                        NetBegin(TAG_ADMIN, "/admin/ban", post);
                    }
                    DrawButton(termBtn, "Terminate", !net.busy);
                    if (ButtonHit(termBtn) && !net.busy) {
                        char post[256] = "";
                        AddField(post, sizeof(post), "targetUsername", srows[i].u);
                        AddField(post, sizeof(post), "token", token);
                        NetBegin(TAG_ADMIN, "/admin/terminate", post);
                    }
                }
                if (canMod) {
                    DrawButton(modBtn, srows[i].role == 1 ? "Revoke Mod" : "Make Mod", !net.busy);
                    if (ButtonHit(modBtn) && !net.busy) {
                        char post[256] = "";
                        AddField(post, sizeof(post), "targetUsername", srows[i].u);
                        AddField(post, sizeof(post), "role", srows[i].role == 1 ? "user" : "moderator");
                        AddField(post, sizeof(post), "token", token);
                        NetBegin(TAG_ADMIN, "/admin/set-role", post);
                    }
                }
            }
        } else if (screen == SCR_LOBBY) {
            if (!inLobby) {
                Rectangle codeBox = {440, 220, 400, 44};
                Rectangle createBtn = {440, 290, 195, 50};
                Rectangle joinBtn = {645, 290, 195, 50};
                Rectangle backBtn = {440, 360, 400, 44};

                if (ButtonHit(codeBox)) focus = 0;
                if (focus == 0) {
                    UpdateTextInput(lobbyCode, sizeof(lobbyCode));
                    for (char *c = lobbyCode; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
                }

                DrawText("Lobby", 560, 100, 40, RED);
                DrawText("Enter a code to join, or leave blank and create", 400, 170, 20, GRAY);
                DrawInputBox(codeBox, lobbyCode, focus == 0);
                DrawButton(createBtn, "Create Lobby", !net.busy);
                DrawButton(joinBtn, "Join Lobby", !net.busy && strlen(lobbyCode) > 0);
                DrawButton(backBtn, "Back to Dashboard", true);
                DrawText(message, 440, 430, 20, YELLOW);

                if (ButtonHit(createBtn) && !net.busy) {
                    char post[256] = "";
                    AddField(post, sizeof(post), "name", myUsername);
                    AddField(post, sizeof(post), "private", "0");
                    AddField(post, sizeof(post), "token", token);
                    NetBegin(TAG_JOIN, "/lobby/create", post);
                }
                if (ButtonHit(joinBtn) && !net.busy && strlen(lobbyCode) > 0) {
                    char post[256] = "";
                    AddField(post, sizeof(post), "code", lobbyCode);
                    AddField(post, sizeof(post), "token", token);
                    NetBegin(TAG_JOIN, "/lobby/join", post);
                }
                if (ButtonHit(backBtn)) {
                    screen = SCR_DASH;
                    message[0] = '\0';
                    focus = 0;
                }
            } else {
                int lawyers = 0;
                for (int i = 0; i < g.players; i++) if (strcmp(g.mem[i].seat, "lawyer") == 0) lawyers++;
                bool isJudge = strcmp(g.seat, "judge") == 0;
                bool canStart = isJudge && lawyers == 2 && strcmp(g.status, "waiting") == 0 && g.round == 1;

                DrawText("Lobby", 40, 30, 40, RED);
                DrawText(TextFormat("Code: %s", lobbyCode), 220, 40, 30, YELLOW);
                DrawText(TextFormat("Players: %d / 15", g.players), 40, 90, 20, WHITE);
                DrawText("Creator is judge. First two joiners are lawyers, the rest spectate.", 40, 120, 20, LIGHTGRAY);

                for (int i = 0; i < g.players; i++) {
                    char line[96];
                    snprintf(line, sizeof(line), "%d. %s - %s", i + 1, g.mem[i].name, g.mem[i].seat);
                    DrawText(line, 60, 160 + i * 26, 20, WHITE);
                }

                Rectangle startBtn = {820, 160, 380, 50};
                Rectangle leaveBtn = {820, 230, 380, 44};
                DrawButton(startBtn, "Start Court", canStart && !net.busy);
                DrawButton(leaveBtn, "Leave Lobby", !net.busy);
                if (!isJudge) DrawText("Waiting for the judge to start...", 820, 295, 20, GRAY);
                DrawText(message, 40, 660, 20, YELLOW);

                if (ButtonHit(startBtn) && canStart && !net.busy) {
                    char post[256] = "";
                    AddField(post, sizeof(post), "code", lobbyCode);
                    AddField(post, sizeof(post), "token", token);
                    NetBegin(TAG_ACT, "/lobby/start", post);
                }
                if (ButtonHit(leaveBtn) && !net.busy) {
                    char post[256] = "";
                    AddField(post, sizeof(post), "code", lobbyCode);
                    AddField(post, sizeof(post), "token", token);
                    NetBegin(TAG_ACT, "/lobby/leave", post);
                    inLobby = false;
                    lobbyCode[0] = '\0';
                    screen = SCR_DASH;
                }
            }
        } else if (screen == SCR_COURT) {
            DrawText("Courtroom", 40, 20, 40, RED);
            DrawText("Defendant: W10L - Windows 10 Lover", 40, 70, 30, WHITE);
            DrawText("Charge: stupidity", 40, 105, 20, YELLOW);

            DrawRectangleRounded((Rectangle){40, 140, 1200, 70}, 0.2f, 8, (Color){40, 30, 60, 255});
            DrawText("W10L stands in the defendant box, proudly defending an unsupported operating system.", 60, 165, 20, WHITE);

            Rectangle lawyerPanels[2] = { {40, 240, 580, 280}, {660, 240, 580, 280} };

            for (int i = 0; i < 2; i++) {
                Rectangle panel = lawyerPanels[i];
                bool mine = g.lidx == i;
                DrawRectangleRounded(panel, 0.06f, 8, (Color){30, 30, 45, 255});
                DrawRectangleLines((int)panel.x, (int)panel.y, (int)panel.width, (int)panel.height, mine ? YELLOW : (Color){80, 80, 110, 255});
                if (g.lname[i][0]) {
                    DrawText(g.lname[i], (int)panel.x + 15, (int)panel.y + 12, 24, RED);
                    if (g.lsub[i]) DrawTextWrapped(g.lroast[i], (int)panel.x + 15, (int)panel.y + 50, (int)panel.width - 30, 20, WHITE);
                    else DrawText("Waiting for roast...", (int)panel.x + 15, (int)panel.y + 50, 20, GRAY);
                } else {
                    DrawText("Missing lawyer", (int)panel.x + 15, (int)panel.y + 12, 24, GRAY);
                }
            }

            bool canType = strcmp(g.seat, "lawyer") == 0 && g.lidx >= 0 && !g.lsub[g.lidx] && strcmp(g.status, "roasting") == 0;
            if (canType) UpdateTextInput(inputRoast, sizeof(inputRoast));

            Rectangle inputBox = {40, 540, 760, 44};
            Rectangle submitBtn = {820, 540, 200, 44};
            Rectangle leaveBtn = {1040, 540, 200, 44};

            DrawInputBox(inputBox, canType ? inputRoast : "", canType);
            DrawButton(submitBtn, "Submit Roast", canType && strlen(inputRoast) > 0 && !net.busy);
            DrawButton(leaveBtn, "Leave Lobby", !net.busy);

            if ((ButtonHit(submitBtn) || (IsKeyPressed(KEY_ENTER) && canType)) && canType && strlen(inputRoast) > 0 && !net.busy) {
                char post[1400] = "";
                AddField(post, sizeof(post), "code", lobbyCode);
                AddField(post, sizeof(post), "token", token);
                AddField(post, sizeof(post), "text", inputRoast);
                if (NetBegin(TAG_ACT, "/lobby/roast", post)) inputRoast[0] = '\0';
            }

            if (strcmp(g.seat, "judge") == 0 && strcmp(g.status, "judging") == 0 && g.lname[0][0] && g.lname[1][0]) {
                DrawText("Judge decision:", 40, 610, 24, YELLOW);
                Rectangle j0 = {240, 600, 300, 44};
                Rectangle j1 = {560, 600, 300, 44};
                char l0[64], l1[64];
                snprintf(l0, sizeof(l0), "%s wins", g.lname[0]);
                snprintf(l1, sizeof(l1), "%s wins", g.lname[1]);
                DrawButton(j0, l0, !net.busy);
                DrawButton(j1, l1, !net.busy);
                int pick = ButtonHit(j0) ? 0 : (ButtonHit(j1) ? 1 : -1);
                if (pick >= 0 && !net.busy) {
                    char post[256] = "", w[8];
                    snprintf(w, sizeof(w), "%d", pick);
                    AddField(post, sizeof(post), "code", lobbyCode);
                    AddField(post, sizeof(post), "token", token);
                    AddField(post, sizeof(post), "winner", w);
                    NetBegin(TAG_ACT, "/lobby/judge", post);
                }
            } else if (strcmp(g.status, "judging") == 0) {
                DrawText("Waiting for the judge's decision...", 40, 610, 20, GRAY);
            } else if (g.winner >= 0 && g.winner < 2 && g.lname[g.winner][0]) {
                char result[128];
                snprintf(result, sizeof(result), "%s won the last round against W10L", g.lname[g.winner]);
                DrawText(result, 40, 610, 24, GREEN);
            } else if (strcmp(g.status, "roasting") == 0 && strcmp(g.seat, "lawyer") != 0) {
                DrawText("Lawyers are roasting W10L...", 40, 610, 20, GRAY);
            }

            if (ButtonHit(leaveBtn) && !net.busy) {
                char post[256] = "";
                AddField(post, sizeof(post), "code", lobbyCode);
                AddField(post, sizeof(post), "token", token);
                NetBegin(TAG_ACT, "/lobby/leave", post);
                inLobby = false;
                lobbyCode[0] = '\0';
                screen = SCR_DASH;
            }
        }

        EndDrawing();
    }

    CloseWindow();
    curl_multi_cleanup(net.m);
    curl_global_cleanup();
    free(net.body.d);
    return 0;
}

#if defined(__MINGW32__)
typedef struct psl_ctx_t psl_ctx_t;
void psl_free(psl_ctx_t *psl) { (void)psl; }
psl_ctx_t *psl_latest(const char *filename) { (void)filename; return NULL; }
psl_ctx_t *psl_builtin(void) { return NULL; }
int psl_is_cookie_domain_acceptable(psl_ctx_t *psl, const char *hostname, const char *domain) {
    (void)psl; (void)hostname; (void)domain; return 1;
}
#endif
