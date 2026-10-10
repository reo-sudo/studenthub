// studenthub: a personal student dashboard for the Nintendo 3DS (unofficial).
// Timetable, assignments, grades, courses and links, stored in a text file on the SD card.
// Built with devkitARM, libctru and citro2d.

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

#define MAX_CLASSES 96
#define MAX_ASG 96
#define MAX_GRADES 48
#define MAX_LINKS 12

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 255)

#define DATA_DIR "sdmc:/3ds/studenthub"
#define DATA_PATH "sdmc:/3ds/studenthub/data.txt"

// ---------- data ----------
typedef struct {
    u8 day; // 0 = Sunday ... 6 = Saturday (same as tm_wday)
    int start, end; // minutes after midnight
    char code[16], name[64], room[16];
} Class;

typedef struct {
    char title[48], code[16];
    int y, m, d;
    u8 status; // 0 not started, 1 in progress, 2 completed
} Assign;

typedef struct {
    char code[16], letter[4];
    int pct;
} Grade;

typedef struct {
    char name[24], url[96];
} Link;

static Class classes[MAX_CLASSES];
static Assign asgs[MAX_ASG];
static Grade grades[MAX_GRADES];
static Link links[MAX_LINKS];
static int classCount, asgCount, gradeCount, linkCount;

static char pname[32] = "Student";
static char degree[64] = "Your degree";
static char term[40] = "Year 1 | Semester 1";
static char uni[16] = "AUT";
static char uniFull[64] = "Auckland University of Technology";
static int semY = 2025, semM = 1, semD = 1;
static int semWeeks = 12;
static int accent = 0; // index into accents
static int depthSet = 2;

static const u32 accents[3] = {RGB(126, 92, 255), RGB(70, 150, 255), RGB(50, 210, 180)};
static const char *accentName[3] = {"Violet", "Blue", "Teal"};
static const float depthMul[3] = {0.0f, 0.55f, 1.0f};
static const char *depthName[3] = {"Off", "Low", "High"};
static const char *dayShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *monShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *statusName[3] = {"Not started", "In progress", "Completed"};

// ---------- ui state ----------
enum { V_HUB, V_TT, V_ASG, V_GRD, V_CRS, V_LNK, V_SET };
static int view = V_HUB;
static int hubSel = 0;
static int ttDay = 1;     // tm_wday of the selected timetable day
static int filt = 0;      // assignment filter
static int lsel = 0, lfirst = 0;
static int setSel = 0;
static u32 confirmFrame = 0;
static u32 frameNo = 0;
static float g3d = 0;
static float gx = 0;

static int ttIdx[MAX_CLASSES], ttCount;
static int aIdx[MAX_ASG], aCount;
static int crsIdx[MAX_CLASSES], crsCount;

static C2D_TextBuf textBuf;
static C3D_RenderTarget *topL, *topR, *botT;

// ---------- small helpers ----------
static u32 shade(u32 c, float f) {
    float r = (c & 255) * f, g = ((c >> 8) & 255) * f, b = ((c >> 16) & 255) * f;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB((u32)r, (u32)g, (u32)b);
}

static u32 withAlpha(u32 c, u32 a) { return (c & 0x00FFFFFF) | (a << 24); }

static float rnd(int i, int k) {
    u32 h = (u32)i * 73856093u ^ (u32)k * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

static u32 codeColor(const char *code) {
    u32 h = 2166136261u;
    for (const char *p = code; *p; p++) h = (h ^ (u8)*p) * 16777619u;
    return RGB(110 + ((h >> 0) & 0x7F), 110 + ((h >> 8) & 0x7F), 110 + ((h >> 16) & 0x7F));
}

static void rect(float x, float y, float w, float h, float z, u32 c) { C2D_DrawRectSolid(x + gx, y, z, w, h, c); }

static void roundRect(float x, float y, float w, float h, float r, float z, u32 c) {
    C2D_DrawRectSolid(x + r + gx, y, z, w - 2 * r, h, c);
    C2D_DrawRectSolid(x + gx, y + r, z, w, h - 2 * r, c);
    C2D_DrawCircleSolid(x + r + gx, y + r, z, r, c);
    C2D_DrawCircleSolid(x + w - r + gx, y + r, z, r, c);
    C2D_DrawCircleSolid(x + r + gx, y + h - r, z, r, c);
    C2D_DrawCircleSolid(x + w - r + gx, y + h - r, z, r, c);
}

// translucent rounded rectangle from non-overlapping strips
static void glass(float x, float y, float w, float h, float r, float z, u32 c) {
    for (int i = 0; i < (int)r; i++) {
        float dy = r - i - 0.5f;
        float inset = r - sqrtf(r * r - dy * dy);
        rect(x + inset, y + i, w - 2 * inset, 1, z, c);
        rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, z, c);
    }
    rect(x, y + r, w, h - 2 * r, z, c);
    rect(x + r, y, w - 2 * r, 1, z + 0.001f, RGBA(255, 255, 255, 70));
}

// align: 0 left, 1 center, 2 right. maxW > 0 shrinks the text to fit.
static void drawText(const char *s, float x, float y, float scale, float maxW, u32 clr, int align) {
    C2D_Text t;
    C2D_TextParse(&t, textBuf, s);
    C2D_TextOptimize(&t);
    float w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    if (maxW > 0 && w > maxW) {
        scale *= maxW / w;
        w = maxW;
    }
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    C2D_DrawText(&t, C2D_WithColor, x + gx, y, 0.9f, scale, scale, clr);
}

static void drawWrapped(const char *s, float x, float y, float scale, float wrapW, u32 clr) {
    C2D_Text t;
    C2D_TextParse(&t, textBuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x + gx, y, 0.9f, scale, scale, clr, wrapW);
}

static void keyChip(const char *label, float cx, float cy) {
    C2D_DrawCircleSolid(cx + gx, cy, 0.6f, 8, RGBA(255, 255, 255, 60));
    drawText(label, cx, cy - 7, 0.45f, 0, RGB(255, 255, 255), 1);
}

// ---------- dates ----------
static time_t dateToTime(int y, int m, int d) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12; // noon avoids daylight-saving edge cases
    t.tm_isdst = -1;
    return mktime(&t);
}

static time_t todayNoon(void) {
    time_t now = time(NULL);
    struct tm lt = *localtime(&now);
    return dateToTime(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

static int daysUntil(int y, int m, int d) {
    return (int)lround(difftime(dateToTime(y, m, d), todayNoon()) / 86400.0);
}

static void ymdFromOffset(int offsetDays, int *y, int *m, int *d) {
    time_t t = todayNoon() + (time_t)offsetDays * 86400;
    struct tm lt = *localtime(&t);
    *y = lt.tm_year + 1900;
    *m = lt.tm_mon + 1;
    *d = lt.tm_mday;
}

static void dueText(const Assign *a, char *out, size_t n) {
    int ty, tm_, td;
    ymdFromOffset(0, &ty, &tm_, &td);
    if (a->y == ty) snprintf(out, n, "Due %d %s", a->d, monShort[(a->m - 1 + 12) % 12]);
    else snprintf(out, n, "Due %d %s %d", a->d, monShort[(a->m - 1 + 12) % 12], a->y);
}

static void daysLeftText(int days, char *out, size_t n) {
    if (days < -1) snprintf(out, n, "%d days ago", -days);
    else if (days == -1) snprintf(out, n, "Yesterday");
    else if (days == 0) snprintf(out, n, "Today");
    else if (days == 1) snprintf(out, n, "Tomorrow");
    else snprintf(out, n, "in %d days", days);
}

static int currentWeek(void) { // 0 = not started yet
    int days = -daysUntil(semY, semM, semD);
    if (days < 0) return 0;
    return days / 7 + 1;
}

static const Class *nextClass(int *daysAhead) {
    time_t now = time(NULL);
    struct tm lt = *localtime(&now);
    int mins = lt.tm_hour * 60 + lt.tm_min;
    for (int d = 0; d < 8; d++) {
        int day = (lt.tm_wday + d) % 7;
        const Class *best = NULL;
        for (int i = 0; i < classCount; i++) {
            const Class *c = &classes[i];
            if (c->day != day) continue;
            if (d == 0 && c->end <= mins) continue;
            if (!best || c->start < best->start) best = c;
        }
        if (best) {
            *daysAhead = d;
            return best;
        }
    }
    return NULL;
}

static int nextDue(void) { // index of the soonest unfinished assignment, or -1
    int best = -1, bestDays = 0;
    for (int i = 0; i < asgCount; i++) {
        if (asgs[i].status == 2) continue;
        int dl = daysUntil(asgs[i].y, asgs[i].m, asgs[i].d);
        if (dl < 0) continue;
        if (best < 0 || dl < bestDays) {
            best = i;
            bestDays = dl;
        }
    }
    return best;
}

// ---------- data file ----------
static int cmpAsg(const void *x, const void *y) {
    const Assign *a = x, *b = y;
    return (a->y * 10000 + a->m * 100 + a->d) - (b->y * 10000 + b->m * 100 + b->d);
}

static void trimLine(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

static int splitBar(char *s, char *f[], int max) {
    int n = 0;
    f[n++] = s;
    for (char *p = s; *p && n < max; p++)
        if (*p == '|') {
            *p = 0;
            f[n++] = p + 1;
        }
    return n;
}

static void copyStr(char *dst, size_t n, const char *src) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

static int parseDay(const char *s) {
    for (int i = 0; i < 7; i++)
        if (strncasecmp(s, dayShort[i], 3) == 0) return i;
    return -1;
}

static int parseTime(const char *s) {
    int h, m;
    if (sscanf(s, "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
}

static void fmtTime(int mins, char *out, size_t n) { snprintf(out, n, "%02d:%02d", mins / 60, mins % 60); }

static bool loadData(void) {
    FILE *f = fopen(DATA_PATH, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        trimLine(line);
        if (!line[0] || line[0] == '#') continue;
        char *fld[8];
        if (strncmp(line, "class|", 6) == 0) {
            int n = splitBar(line + 6, fld, 8);
            if (n >= 6 && classCount < MAX_CLASSES) {
                Class *c = &classes[classCount];
                int day = parseDay(fld[0]), st = parseTime(fld[1]), en = parseTime(fld[2]);
                if (day < 0 || st < 0 || en < 0) continue;
                c->day = (u8)day;
                c->start = st;
                c->end = en;
                copyStr(c->code, sizeof(c->code), fld[3]);
                copyStr(c->name, sizeof(c->name), fld[4]);
                copyStr(c->room, sizeof(c->room), fld[5]);
                classCount++;
            }
        } else if (strncmp(line, "assignment|", 11) == 0) {
            int n = splitBar(line + 11, fld, 8);
            int y, m, d, st;
            if (n >= 4 && asgCount < MAX_ASG && sscanf(fld[0], "%d-%d-%d", &y, &m, &d) == 3) {
                Assign *a = &asgs[asgCount];
                a->y = y;
                a->m = m;
                a->d = d;
                copyStr(a->code, sizeof(a->code), fld[1]);
                copyStr(a->title, sizeof(a->title), fld[2]);
                st = atoi(fld[3]);
                a->status = (u8)(st < 0 ? 0 : st > 2 ? 2 : st);
                asgCount++;
            }
        } else if (strncmp(line, "grade|", 6) == 0) {
            int n = splitBar(line + 6, fld, 8);
            if (n >= 3 && gradeCount < MAX_GRADES) {
                Grade *g = &grades[gradeCount];
                copyStr(g->code, sizeof(g->code), fld[0]);
                copyStr(g->letter, sizeof(g->letter), fld[1]);
                g->pct = atoi(fld[2]);
                gradeCount++;
            }
        } else if (strncmp(line, "link|", 5) == 0) {
            int n = splitBar(line + 5, fld, 8);
            if (n >= 2 && linkCount < MAX_LINKS) {
                copyStr(links[linkCount].name, sizeof(links[linkCount].name), fld[0]);
                copyStr(links[linkCount].url, sizeof(links[linkCount].url), fld[1]);
                linkCount++;
            }
        } else {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            const char *k = line, *v = eq + 1;
            if (!strcmp(k, "name")) copyStr(pname, sizeof(pname), v);
            else if (!strcmp(k, "degree")) copyStr(degree, sizeof(degree), v);
            else if (!strcmp(k, "term")) copyStr(term, sizeof(term), v);
            else if (!strcmp(k, "uni")) copyStr(uni, sizeof(uni), v);
            else if (!strcmp(k, "unifull")) copyStr(uniFull, sizeof(uniFull), v);
            else if (!strcmp(k, "semstart")) sscanf(v, "%d-%d-%d", &semY, &semM, &semD);
            else if (!strcmp(k, "weeks")) semWeeks = atoi(v) > 0 ? atoi(v) : 12;
            else if (!strcmp(k, "accent")) accent = atoi(v) % 3 < 0 ? 0 : atoi(v) % 3;
            else if (!strcmp(k, "depth")) depthSet = atoi(v) % 3 < 0 ? 0 : atoi(v) % 3;
        }
    }
    fclose(f);
    qsort(asgs, asgCount, sizeof(Assign), cmpAsg);
    return true;
}

static void saveData(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    FILE *f = fopen(DATA_PATH, "w");
    if (!f) return;
    fprintf(f, "# Student Hub data. You can edit this file on a computer or change things inside the app.\n");
    fprintf(f, "# Lines: class|Day|Start|End|Code|Name|Room   assignment|YYYY-MM-DD|Code|Title|Status(0-2)\n");
    fprintf(f, "#        grade|Code|Letter|Percent   link|Name|URL\n");
    fprintf(f, "name=%s\ndegree=%s\nterm=%s\nuni=%s\nunifull=%s\n", pname, degree, term, uni, uniFull);
    fprintf(f, "semstart=%04d-%02d-%02d\nweeks=%d\naccent=%d\ndepth=%d\n", semY, semM, semD, semWeeks, accent, depthSet);
    for (int i = 0; i < classCount; i++) {
        char a[8], b[8];
        fmtTime(classes[i].start, a, sizeof(a));
        fmtTime(classes[i].end, b, sizeof(b));
        fprintf(f, "class|%s|%s|%s|%s|%s|%s\n", dayShort[classes[i].day], a, b, classes[i].code, classes[i].name,
                classes[i].room);
    }
    for (int i = 0; i < asgCount; i++)
        fprintf(f, "assignment|%04d-%02d-%02d|%s|%s|%d\n", asgs[i].y, asgs[i].m, asgs[i].d, asgs[i].code, asgs[i].title,
                asgs[i].status);
    for (int i = 0; i < gradeCount; i++)
        fprintf(f, "grade|%s|%s|%d\n", grades[i].code, grades[i].letter, grades[i].pct);
    for (int i = 0; i < linkCount; i++) fprintf(f, "link|%s|%s\n", links[i].name, links[i].url);
    fclose(f);
}

static void addClass(int day, const char *start, const char *end, const char *code, const char *name, const char *room) {
    Class *c = &classes[classCount++];
    c->day = (u8)day;
    c->start = parseTime(start);
    c->end = parseTime(end);
    copyStr(c->code, sizeof(c->code), code);
    copyStr(c->name, sizeof(c->name), name);
    copyStr(c->room, sizeof(c->room), room);
}

static void addAsgOffset(int offset, const char *code, const char *title, int status) {
    Assign *a = &asgs[asgCount++];
    ymdFromOffset(offset, &a->y, &a->m, &a->d);
    copyStr(a->code, sizeof(a->code), code);
    copyStr(a->title, sizeof(a->title), title);
    a->status = (u8)status;
}

static void addGrade(const char *code, const char *letter, int pct) {
    Grade *g = &grades[gradeCount++];
    copyStr(g->code, sizeof(g->code), code);
    copyStr(g->letter, sizeof(g->letter), letter);
    g->pct = pct;
}

// sample data so the app is useful the first time it runs
static void createSample(void) {
    time_t now = time(NULL);
    struct tm lt = *localtime(&now);
    int sinceMon = (lt.tm_wday + 6) % 7;
    ymdFromOffset(-(sinceMon + 28), &semY, &semM, &semD); // so the first run shows "Week 5"

    addClass(2, "10:00", "11:00", "COMP500", "Programming Concepts and Techniques", "WA220");
    addClass(2, "13:00", "14:00", "COMP508", "Database Systems", "WG403");
    addClass(2, "16:00", "17:00", "COMP504", "Networks", "WZ514");
    addClass(3, "09:00", "11:00", "CITS101", "Professional Practice", "WG402");
    addClass(4, "11:00", "13:00", "COMP500", "Programming Lab", "WA220");
    addClass(5, "14:00", "16:00", "COMP504", "Networks Lab", "WZ514");

    addAsgOffset(3, "COMP504", "Group Assignment", 1);
    addAsgOffset(10, "CITS101", "Reflection Journal", 0);
    addAsgOffset(17, "COMP508", "Project Proposal", 0);
    addAsgOffset(-12, "COMP500", "Programming Quiz", 2);

    addGrade("COMP500", "A-", 87);
    addGrade("COMP504", "B+", 82);
    addGrade("COMP508", "B", 76);
    addGrade("CITS101", "A", 91);

    qsort(asgs, asgCount, sizeof(Assign), cmpAsg);
    saveData();
}

// ---------- keyboard prompts ----------
static bool askText(const char *hint, const char *initial, char *out, size_t outSize, int maxLen) {
    SwkbdState s;
    swkbdInit(&s, SWKBD_TYPE_NORMAL, 2, maxLen);
    swkbdSetHintText(&s, hint);
    if (initial && initial[0]) swkbdSetInitialText(&s, initial);
    swkbdSetValidation(&s, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    memset(out, 0, outSize);
    SwkbdButton b = swkbdInputText(&s, out, outSize);
    if (b != SWKBD_BUTTON_CONFIRM) return false;
    for (char *p = out; *p; p++)
        if (*p == '|' || *p == '\n' || *p == '\r') *p = '/'; // the data file uses | as a separator
    return true;
}

static bool askDate(const char *hint, int *y, int *m, int *d) {
    char buf[32];
    for (;;) {
        if (!askText(hint, NULL, buf, sizeof(buf), 10)) return false;
        if (sscanf(buf, "%d-%d-%d", y, m, d) == 3 && *m >= 1 && *m <= 12 && *d >= 1 && *d <= 31 && *y >= 2000) return true;
    }
}

static void upper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void promptAddAssignment(void) {
    if (asgCount >= MAX_ASG) return;
    char title[48], code[16];
    int y, m, d;
    if (!askText("Assignment title", NULL, title, sizeof(title), 40)) return;
    if (!askText("Course code, e.g. COMP500", NULL, code, sizeof(code), 10)) return;
    if (!askDate("Due date as YYYY-MM-DD", &y, &m, &d)) return;
    upper(code);
    Assign *a = &asgs[asgCount++];
    copyStr(a->title, sizeof(a->title), title);
    copyStr(a->code, sizeof(a->code), code);
    a->y = y;
    a->m = m;
    a->d = d;
    a->status = 0;
    qsort(asgs, asgCount, sizeof(Assign), cmpAsg);
}

static void promptAddGrade(void) {
    if (gradeCount >= MAX_GRADES) return;
    char code[16], letter[8], pct[8];
    if (!askText("Course code, e.g. COMP500", NULL, code, sizeof(code), 10)) return;
    if (!askText("Letter grade, e.g. A-", NULL, letter, sizeof(letter), 2)) return;
    for (;;) {
        if (!askText("Result percentage (0 to 100)", NULL, pct, sizeof(pct), 3)) return;
        int p = atoi(pct);
        if (p >= 0 && p <= 100) {
            upper(code);
            addGrade(code, letter, p);
            return;
        }
    }
}

// ---------- list views ----------
static void rebuildLists(void) {
    // timetable for the selected day, sorted by start time
    ttCount = 0;
    for (int i = 0; i < classCount; i++)
        if (classes[i].day == ttDay) ttIdx[ttCount++] = i;
    for (int i = 1; i < ttCount; i++) {
        int k = ttIdx[i], j = i - 1;
        while (j >= 0 && classes[ttIdx[j]].start > classes[k].start) {
            ttIdx[j + 1] = ttIdx[j];
            j--;
        }
        ttIdx[j + 1] = k;
    }
    // assignments for the current filter (array is already sorted by date)
    aCount = 0;
    for (int i = 0; i < asgCount; i++)
        if (asgs[i].status == filt) aIdx[aCount++] = i;
    // distinct courses, taken from the timetable
    crsCount = 0;
    for (int i = 0; i < classCount; i++) {
        bool seen = false;
        for (int j = 0; j < crsCount; j++)
            if (!strcmp(classes[crsIdx[j]].code, classes[i].code)) seen = true;
        if (!seen) crsIdx[crsCount++] = i;
    }
}

static int listCount(void) {
    switch (view) {
    case V_TT: return ttCount;
    case V_ASG: return aCount;
    case V_GRD: return gradeCount;
    case V_CRS: return crsCount;
    case V_LNK: return linkCount;
    }
    return 0;
}

static void listGeom(float *top, float *h, int *vis) {
    switch (view) {
    case V_TT:
    case V_ASG: *top = 68; *h = 44; *vis = 3; break;
    case V_GRD: *top = 62; *h = 28; *vis = 5; break;
    default: *top = 44; *h = 50; *vis = 3; break;
    }
}

static void clampList(void) {
    int n = listCount();
    float top, h;
    int vis;
    listGeom(&top, &h, &vis);
    if (lsel >= n) lsel = n ? n - 1 : 0;
    if (lsel < 0) lsel = 0;
    if (lsel < lfirst) lfirst = lsel;
    if (lsel >= lfirst + vis) lfirst = lsel - vis + 1;
    if (lfirst < 0) lfirst = 0;
}

static void openView(int v) {
    view = v;
    lsel = lfirst = 0;
    confirmFrame = 0;
    if (v == V_TT) {
        time_t now = time(NULL);
        ttDay = localtime(&now)->tm_wday;
    }
    rebuildLists();
    clampList();
}

// ---------- drawing ----------
#define LAYER(dz) (gx = (eye ? -1.0f : 1.0f) * parallax * (dz) * 8.0f)

static void drawTop(int eye) {
    float parallax = g3d * depthMul[depthSet];
    float t = frameNo / 60.0f;
    u32 white = RGB(255, 255, 255);
    u32 dim = RGBA(255, 255, 255, 190);
    u32 acc = accents[accent];

    // night sky
    LAYER(-1.0f);
    u32 s1 = RGB(8, 10, 36), s2 = RGB(44, 30, 100), s3 = RGB(98, 54, 142);
    C2D_DrawRectangle(gx - 30, 0, 0.00f, TOP_W + 60, 110, s1, s1, s2, s2);
    C2D_DrawRectangle(gx - 30, 110, 0.00f, TOP_W + 60, 130, s2, s2, s3, s3);

    LAYER(-0.9f);
    for (int i = 0; i < 40; i++) {
        float tw = 0.5f + 0.5f * sinf(t * 2.0f + i * 1.7f);
        rect(rnd(i, 1) * TOP_W, rnd(i, 2) * 90, 1, 1, 0.05f, RGBA(255, 255, 255, (u32)(60 + tw * 140)));
    }

    // campus building with lit windows
    LAYER(-0.5f);
    u32 bcol = RGB(22, 20, 58);
    rect(190, 58, 230, 132, 0.10f, bcol);
    rect(128, 112, 70, 78, 0.10f, shade(bcol, 1.25f));
    for (int i = 0; i < 18; i++)
        for (int j = 0; j < 12; j++) {
            float r = rnd(i * 31 + j, 5);
            bool lit = r > 0.5f;
            rect(198 + i * 12, 66 + j * 10, 7, 6, 0.12f,
                 lit ? RGBA(255, 214, 140, (u32)(110 + r * 120)) : RGBA(90, 100, 180, 60));
        }
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 7; j++) {
            float r = rnd(i * 17 + j, 6);
            rect(136 + i * 11, 120 + j * 10, 6, 6, 0.12f,
                 r > 0.45f ? RGBA(255, 214, 140, (u32)(100 + r * 120)) : RGBA(90, 100, 180, 60));
        }

    // trees and ground
    LAYER(-0.3f);
    u32 tree = RGB(14, 12, 38);
    for (int i = 0; i < 9; i++) C2D_DrawCircleSolid(gx - 6 + i * 22, 176 + rnd(i, 7) * 10, 0.15f, 16 + rnd(i, 8) * 8, tree);
    LAYER(-0.2f);
    u32 g1 = RGB(30, 22, 66), g2 = RGB(10, 8, 28);
    C2D_DrawRectangle(gx - 30, 188, 0.20f, TOP_W + 60, 52, g1, g1, g2, g2);
    rect(-30, 188, TOP_W + 60, 2, 0.21f, withAlpha(acc, 140));

    // university name and greeting
    LAYER(0.6f);
    drawText(uni, 16, 26, 1.25f, 120, white, 0);
    drawText(uniFull, 18, 70, 0.32f, 150, dim, 0);
    drawText("Kia ora,", 16, 92, 0.5f, 0, dim, 0);
    drawText(pname, 16, 106, 0.95f, 200, white, 0);
    drawWrapped(degree, 16, 136, 0.4f, 180, dim);
    drawText(term, 16, 160, 0.4f, 200, shade(acc, 1.3f), 0);

    // week and next class card
    LAYER(0.5f);
    glass(232, 32, 156, 128, 10, 0.40f, RGBA(255, 255, 255, 40));
    LAYER(0.8f);
    int wk = currentWeek();
    char buf[96];
    drawText("Current Week", 244, 40, 0.42f, 0, dim, 0);
    if (wk == 0) snprintf(buf, sizeof(buf), "Not started");
    else if (wk > semWeeks) snprintf(buf, sizeof(buf), "Break");
    else snprintf(buf, sizeof(buf), "Week %d", wk);
    drawText(buf, 244, 54, 0.8f, 130, white, 0);
    float prog = wk <= 0 ? 0 : (wk > semWeeks ? 1 : (float)wk / semWeeks);
    roundRect(244, 90, 132, 6, 3, 0.45f, RGB(60, 56, 120));
    if (prog > 0.02f) roundRect(244, 90, 132 * prog, 6, 3, 0.46f, acc);
    rect(244, 104, 132, 1, 0.45f, RGBA(255, 255, 255, 50));

    int ahead = 0;
    const Class *nc = nextClass(&ahead);
    drawText("Next Class", 244, 110, 0.42f, 0, dim, 0);
    if (nc) {
        char a[8], b[8];
        fmtTime(nc->start, a, sizeof(a));
        fmtTime(nc->end, b, sizeof(b));
        drawText(nc->code, 244, 122, 0.65f, 80, white, 0);
        drawText(nc->room, 376, 128, 0.45f, 50, dim, 2);
        const char *dn = ahead == 0 ? "Today" : (ahead == 1 ? "Tomorrow" : dayShort[nc->day]);
        snprintf(buf, sizeof(buf), "%s %s - %s", dn, a, b);
        drawText(buf, 244, 144, 0.42f, 132, dim, 0);
    } else {
        drawText("No classes set", 244, 126, 0.5f, 132, white, 0);
    }

    // due soon strip
    LAYER(0.4f);
    glass(16, 170, 368, 24, 8, 0.40f, RGBA(255, 255, 255, 36));
    int nd = nextDue();
    if (nd >= 0) {
        char dl[32];
        int days = daysUntil(asgs[nd].y, asgs[nd].m, asgs[nd].d);
        daysLeftText(days, dl, sizeof(dl));
        snprintf(buf, sizeof(buf), "Next due: %s (%s)  %s", asgs[nd].title, asgs[nd].code, dl);
        drawText(buf, 26, 174, 0.42f, 348, days <= 2 ? RGB(255, 170, 160) : white, 0);
    } else {
        drawText("Nothing due. Nice work.", 26, 174, 0.42f, 348, dim, 0);
    }

    // status bar (no depth)
    LAYER(0.0f);
    rect(0, 0, TOP_W, 22, 0.70f, RGBA(0, 0, 0, 100));
    u8 wifi = osGetWifiStrength();
    for (int i = 0; i < 4; i++)
        rect(8 + i * 5, 17 - i * 3, 3, 3 + i * 3, 0.80f, i <= wifi ? white : RGBA(255, 255, 255, 70));
    snprintf(buf, sizeof(buf), "%s Student", uni);
    drawText(buf, 34, 3, 0.45f, 140, white, 0);

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    strftime(buf, sizeof(buf), "%a, %d %b  %H:%M", tmv);
    drawText(buf, TOP_W - 52, 3, 0.45f, 0, white, 2);

    u8 lvl = 0, charging = 0;
    PTMU_GetBatteryLevel(&lvl);
    PTMU_GetBatteryChargeState(&charging);
    rect(TOP_W - 44, 5, 34, 13, 0.80f, RGBA(255, 255, 255, 220));
    rect(TOP_W - 10, 8, 3, 7, 0.80f, RGBA(255, 255, 255, 220));
    rect(TOP_W - 43, 6, 32, 11, 0.82f, RGBA(20, 24, 40, 255));
    rect(TOP_W - 43, 6, 32.0f * lvl / 5.0f, 11, 0.84f, charging ? acc : RGB(80, 210, 110));

    keyChip("A", 16, 222);
    drawText("Open", 30, 214, 0.42f, 0, white, 0);
    keyChip("B", 78, 222);
    drawText("Back", 92, 214, 0.42f, 0, white, 0);
    gx = 0;
}

static void glyph(int k, float cx, float cy, u32 c) {
    u32 in = RGB(22, 18, 54);
    float z = 0.60f;
    switch (k) {
    case 0: // calendar
        roundRect(cx - 12, cy - 11, 24, 22, 3, z, c);
        rect(cx - 9, cy - 4, 18, 12, z + 0.01f, in);
        for (int i = 0; i < 3; i++) rect(cx - 7 + i * 6, cy - 1, 4, 3, z + 0.02f, c);
        for (int i = 0; i < 3; i++) rect(cx - 7 + i * 6, cy + 4, 4, 3, z + 0.02f, c);
        break;
    case 1: // document
        roundRect(cx - 9, cy - 12, 18, 24, 3, z, c);
        for (int i = 0; i < 4; i++) rect(cx - 6, cy - 7 + i * 5, 12, 2, z + 0.01f, in);
        break;
    case 2: // bars
        rect(cx - 11, cy + 2, 6, 9, z, c);
        rect(cx - 3, cy - 5, 6, 16, z, c);
        rect(cx + 5, cy - 11, 6, 22, z, c);
        break;
    case 3: // book
        roundRect(cx - 13, cy - 9, 12, 18, 2, z, c);
        roundRect(cx + 1, cy - 9, 12, 18, 2, z, c);
        break;
    case 4: // globe
        C2D_DrawCircleSolid(cx, cy, z, 12, c);
        C2D_DrawCircleSolid(cx, cy, z + 0.01f, 9.5f, in);
        rect(cx - 0.75f, cy - 9, 1.5f, 18, z + 0.02f, c);
        rect(cx - 9, cy - 0.75f, 18, 1.5f, z + 0.02f, c);
        break;
    default: // gear
        C2D_DrawCircleSolid(cx, cy, z, 8, c);
        for (int i = 0; i < 8; i++) {
            float a = i * 0.785398f;
            C2D_DrawCircleSolid(cx + cosf(a) * 10, cy + sinf(a) * 10, z, 2.6f, c);
        }
        C2D_DrawCircleSolid(cx, cy, z + 0.01f, 3.5f, in);
        break;
    }
}

static void header(const char *title, const char *right) {
    u32 acc = accents[accent];
    glass(8, 6, 304, 28, 10, 0.20f, RGBA(255, 255, 255, 30));
    drawText("<", 20, 8, 0.7f, 0, acc, 0);
    drawText(title, 40, 10, 0.6f, 180, RGB(255, 255, 255), 0);
    if (right) drawText(right, 302, 12, 0.45f, 110, RGBA(255, 255, 255, 190), 2);
}

static void footer(const char *left, const char *mid, const char *right) {
    glass(0, 208, BOT_W, 32, 2, 0.20f, RGBA(255, 255, 255, 26));
    keyChip("B", 20, 224);
    drawText(left, 34, 216, 0.5f, 60, RGB(255, 255, 255), 0);
    if (mid) drawText(mid, BOT_W / 2, 218, 0.38f, 150, RGBA(255, 255, 255, 190), 1);
    if (right) {
        keyChip("A", BOT_W - 72, 224);
        drawText(right, BOT_W - 10, 216, 0.5f, 56, RGB(255, 255, 255), 2);
    }
}

static void drawBackground(void) {
    u32 b1 = RGB(10, 10, 34), b2 = RGB(30, 22, 70);
    C2D_DrawRectangle(0, 0, 0.0f, BOT_W, SCR_H, b1, b1, b2, b2);
}

static void card(float x, float y, float w, float h, u32 strip, bool sel) {
    glass(x, y, w, h, 8, 0.30f, RGBA(255, 255, 255, sel ? 62 : 26));
    rect(x + 2, y + 6, 3, h - 12, 0.40f, strip);
}

static void drawHub(void) {
    u32 acc = accents[accent];
    u32 white = RGB(255, 255, 255);
    glass(8, 6, 304, 28, 10, 0.20f, RGBA(255, 255, 255, 30));
    drawText("Student Hub", 22, 10, 0.6f, 200, white, 0);

    static const char *labels[6] = {"Timetable", "Assignments", "Grades", "Courses", "Links", "Settings"};
    static const u32 tileCol[6] = {RGB(150, 112, 255), RGB(90, 160, 255), RGB(60, 210, 180), RGB(250, 180, 70),
                                   RGB(250, 100, 140), RGB(150, 160, 190)};
    for (int i = 0; i < 6; i++) {
        float x = 8 + (i % 3) * 104, y = 42 + (i / 3) * 70;
        bool on = (i == hubSel);
        glass(x, y, 96, 62, 10, 0.30f, RGBA(255, 255, 255, on ? 20 : 26));
        if (on) roundRect(x, y, 96, 62, 10, 0.31f, shade(tileCol[i], 0.55f));
        glyph(i, x + 48, y + 24, on ? white : tileCol[i]);
        drawText(labels[i], x + 48, y + 44, 0.45f, 88, white, 1);
        if (on) {
            float pulse = 0.5f + 0.5f * sinf(frameNo * 0.1f);
            u32 c = withAlpha(acc, (u32)(150 + 105 * pulse));
            rect(x - 3, y - 3, 22, 3, 0.80f, c);
            rect(x - 3, y - 3, 3, 22, 0.80f, c);
            rect(x + 77, y - 3, 22, 3, 0.80f, c);
            rect(x + 96, y - 3, 3, 22, 0.80f, c);
            rect(x - 3, y + 62, 22, 3, 0.80f, c);
            rect(x - 3, y + 43, 3, 22, 0.80f, c);
            rect(x + 77, y + 62, 22, 3, 0.80f, c);
            rect(x + 96, y + 43, 3, 22, 0.80f, c);
        }
    }

    char buf[64];
    int wk = currentWeek();
    int due = 0;
    for (int i = 0; i < asgCount; i++) {
        int dl = daysUntil(asgs[i].y, asgs[i].m, asgs[i].d);
        if (asgs[i].status != 2 && dl >= 0 && dl <= 7) due++;
    }
    if (wk > 0 && wk <= semWeeks) snprintf(buf, sizeof(buf), "Week %d   |   %d due in the next 7 days", wk, due);
    else snprintf(buf, sizeof(buf), "%d due in the next 7 days", due);
    glass(8, 182, 304, 22, 8, 0.20f, RGBA(255, 255, 255, 34));
    drawText(buf, BOT_W / 2, 185, 0.45f, 290, RGBA(255, 255, 255, 220), 1);

    footer("Exit", "D-pad to move", "Open");
}

static void drawTimetable(void) {
    u32 acc = accents[accent];
    u32 white = RGB(255, 255, 255), dim = RGBA(255, 255, 255, 170);
    time_t now = time(NULL);
    int today = localtime(&now)->tm_wday;
    header("Timetable", dayShort[ttDay]);

    for (int i = 0; i < 7; i++) {
        int wd = (i + 1) % 7; // Monday first
        float x = 8 + i * 43;
        if (wd == ttDay) roundRect(x, 40, 41, 22, 7, 0.30f, acc);
        else glass(x, 40, 41, 22, 7, 0.30f, RGBA(255, 255, 255, 24));
        drawText(dayShort[wd], x + 20.5f, 43, 0.45f, 38, wd == ttDay ? white : dim, 1);
        if (wd == today) C2D_DrawCircleSolid(x + 20.5f, 59, 0.5f, 1.8f, wd == ttDay ? white : acc);
    }

    if (ttCount == 0) {
        drawText("No classes this day", BOT_W / 2, 110, 0.5f, 290, dim, 1);
    } else {
        for (int i = lfirst; i < ttCount && i < lfirst + 3; i++) {
            const Class *c = &classes[ttIdx[i]];
            float y = 68 + (i - lfirst) * 46;
            bool on = (i == lsel);
            card(8, y, 304, 42, codeColor(c->code), on);
            char a[8], b[8], tb[24];
            fmtTime(c->start, a, sizeof(a));
            fmtTime(c->end, b, sizeof(b));
            snprintf(tb, sizeof(tb), "%s - %s", a, b);
            drawText(c->code, 18, y + 5, 0.55f, 100, white, 0);
            drawText(tb, 300, y + 6, 0.45f, 100, white, 2);
            drawText(c->name, 18, y + 24, 0.4f, 210, dim, 0);
            drawText(c->room, 300, y + 24, 0.4f, 60, dim, 2);
        }
    }
    footer("Back", "L / R change day", NULL);
}

static void drawAssignments(void) {
    u32 acc = accents[accent];
    u32 white = RGB(255, 255, 255), dim = RGBA(255, 255, 255, 170);
    header("Assignments", NULL);
    static const char *tabs[3] = {"Upcoming", "In Progress", "Completed"};
    for (int i = 0; i < 3; i++) {
        float x = 8 + i * 102;
        if (i == filt) roundRect(x, 40, 100, 22, 7, 0.30f, acc);
        else glass(x, 40, 100, 22, 7, 0.30f, RGBA(255, 255, 255, 24));
        drawText(tabs[i], x + 50, 43, 0.45f, 94, i == filt ? white : dim, 1);
    }

    if (aCount == 0) {
        drawText("Nothing here. Press X to add one.", BOT_W / 2, 110, 0.45f, 290, dim, 1);
    } else {
        for (int i = lfirst; i < aCount && i < lfirst + 3; i++) {
            const Assign *a = &asgs[aIdx[i]];
            float y = 68 + (i - lfirst) * 46;
            bool on = (i == lsel);
            card(8, y, 304, 42, codeColor(a->code), on);
            char due[40], left[32], line[96];
            dueText(a, due, sizeof(due));
            int days = daysUntil(a->y, a->m, a->d);
            daysLeftText(days, left, sizeof(left));
            drawText(a->title, 18, y + 5, 0.5f, 200, white, 0);
            snprintf(line, sizeof(line), "%s  -  %s", a->code, due);
            drawText(line, 18, y + 24, 0.4f, 200, dim, 0);
            u32 lc = (a->status != 2 && days <= 2) ? RGB(255, 150, 140) : dim;
            drawText(a->status == 2 ? "Done" : left, 300, y + 6, 0.42f, 80, lc, 2);
            drawText(statusName[a->status], 300, y + 24, 0.4f, 80, acc, 2);
        }
    }
    bool confirm = confirmFrame && frameNo - confirmFrame < 180;
    footer("Back", confirm ? "Press Y again to delete" : "X add   Y delete   L/R filter", aCount ? "Status" : NULL);
}

static void drawGrades(void) {
    u32 acc = accents[accent];
    u32 white = RGB(255, 255, 255), dim = RGBA(255, 255, 255, 170);
    int sum = 0;
    for (int i = 0; i < gradeCount; i++) sum += grades[i].pct;
    char buf[48];
    if (gradeCount) snprintf(buf, sizeof(buf), "Average %d%%", sum / gradeCount);
    else snprintf(buf, sizeof(buf), "No results yet");
    header("Grades", buf);

    for (int i = lfirst; i < gradeCount && i < lfirst + 5; i++) {
        const Grade *g = &grades[i];
        float y = 62 + (i - lfirst) * 28;
        bool on = (i == lsel);
        glass(8, y, 304, 25, 7, 0.30f, RGBA(255, 255, 255, on ? 62 : 26));
        rect(10, y + 5, 3, 15, 0.40f, codeColor(g->code));
        drawText(g->code, 20, y + 4, 0.5f, 90, white, 0);
        drawText(g->letter, 128, y + 4, 0.5f, 30, acc, 0);
        roundRect(168, y + 9, 90, 6, 3, 0.40f, RGB(60, 56, 120));
        float p = g->pct < 0 ? 0 : (g->pct > 100 ? 1 : g->pct / 100.0f);
        if (p > 0.03f) roundRect(168, y + 9, 90 * p, 6, 3, 0.41f, acc);
        snprintf(buf, sizeof(buf), "%d%%", g->pct);
        drawText(buf, 302, y + 4, 0.5f, 40, white, 2);
    }
    if (!gradeCount) drawText("Press X to add a result", BOT_W / 2, 110, 0.45f, 290, dim, 1);
    bool confirm = confirmFrame && frameNo - confirmFrame < 180;
    footer("Back", confirm ? "Press Y again to delete" : "X add   Y delete", NULL);
}

static void drawCourses(void) {
    u32 white = RGB(255, 255, 255), dim = RGBA(255, 255, 255, 170);
    header("Course Info", NULL);
    if (!crsCount) drawText("Add classes in data.txt to see courses", BOT_W / 2, 110, 0.42f, 290, dim, 1);
    for (int i = lfirst; i < crsCount && i < lfirst + 3; i++) {
        const Class *c = &classes[crsIdx[i]];
        float y = 44 + (i - lfirst) * 54;
        int n = 0;
        char rooms[48] = "";
        for (int j = 0; j < classCount; j++)
            if (!strcmp(classes[j].code, c->code)) {
                n++;
                if (!strstr(rooms, classes[j].room) && strlen(rooms) + strlen(classes[j].room) + 3 < sizeof(rooms)) {
                    if (rooms[0]) strcat(rooms, ", ");
                    strcat(rooms, classes[j].room);
                }
            }
        card(8, y, 304, 50, codeColor(c->code), i == lsel);
        drawText(c->code, 18, y + 5, 0.6f, 100, white, 0);
        drawText(c->name, 18, y + 26, 0.42f, 280, dim, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d session%s a week", n, n == 1 ? "" : "s");
        drawText(buf, 300, y + 7, 0.4f, 120, dim, 2);
        drawText(rooms, 300, y + 26, 0.4f, 100, white, 2);
    }
    footer("Back", NULL, NULL);
}

static void drawLinks(void) {
    u32 white = RGB(255, 255, 255), dim = RGBA(255, 255, 255, 170);
    header("Links", NULL);
    if (!linkCount) {
        drawText("No links yet", BOT_W / 2, 90, 0.55f, 290, white, 1);
        drawText("Add lines like link|Canvas|https://... to data.txt", BOT_W / 2, 116, 0.38f, 290, dim, 1);
    }
    for (int i = lfirst; i < linkCount && i < lfirst + 3; i++) {
        float y = 44 + (i - lfirst) * 54;
        card(8, y, 304, 50, accents[accent], i == lsel);
        drawText(links[i].name, 18, y + 5, 0.6f, 280, white, 0);
        drawText(links[i].url, 18, y + 28, 0.38f, 285, dim, 0);
    }
    footer("Back", "Type the address into the browser", NULL);
}

static void settingRow(int i, const char *label, const char *value) {
    u32 acc = accents[accent];
    float y = 44 + i * 26;
    bool on = (i == setSel);
    glass(8, y, 304, 24, 7, 0.30f, RGBA(255, 255, 255, on ? 62 : 26));
    if (on) roundRect(8, y, 4, 24, 2, 0.40f, acc);
    drawText(label, 22, y + 4, 0.48f, 110, RGB(255, 255, 255), 0);
    drawText(value, 302, y + 4, 0.45f, 170, acc, 2);
}

static void drawSettings(void) {
    char buf[32];
    header("Settings", NULL);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", semY, semM, semD);
    settingRow(0, "Name", pname);
    settingRow(1, "Degree", degree);
    settingRow(2, "Year and term", term);
    settingRow(3, "Semester starts", buf);
    settingRow(4, "Accent colour", accentName[accent]);
    settingRow(5, "3D depth", depthName[depthSet]);
    footer("Back", "Unofficial student app", "Change");
}

static void drawBottom(void) {
    gx = 0;
    drawBackground();
    switch (view) {
    case V_HUB: drawHub(); break;
    case V_TT: drawTimetable(); break;
    case V_ASG: drawAssignments(); break;
    case V_GRD: drawGrades(); break;
    case V_CRS: drawCourses(); break;
    case V_LNK: drawLinks(); break;
    default: drawSettings(); break;
    }
}

// ---------- actions ----------
static void changeSetting(int dir, bool edit) {
    char buf[96];
    if (setSel == 0 && edit) {
        if (askText("Your name", pname, buf, sizeof(buf), 24)) copyStr(pname, sizeof(pname), buf);
    } else if (setSel == 1 && edit) {
        if (askText("Your degree", degree, buf, sizeof(buf), 60)) copyStr(degree, sizeof(degree), buf);
    } else if (setSel == 2 && edit) {
        if (askText("Year and term, e.g. Year 2 | Sem 1 2026", term, buf, sizeof(buf), 36)) copyStr(term, sizeof(term), buf);
    } else if (setSel == 3 && edit) {
        int y, m, d;
        if (askDate("Semester start (a Monday) as YYYY-MM-DD", &y, &m, &d)) {
            semY = y;
            semM = m;
            semD = d;
        }
    } else if (setSel == 4) accent = (accent + dir + 3) % 3;
    else if (setSel == 5) depthSet = (depthSet + dir + 3) % 3;
}

static void deleteSelected(void) {
    if (view == V_ASG && aCount) {
        int idx = aIdx[lsel];
        for (int i = idx; i < asgCount - 1; i++) asgs[i] = asgs[i + 1];
        asgCount--;
    } else if (view == V_GRD && gradeCount) {
        for (int i = lsel; i < gradeCount - 1; i++) grades[i] = grades[i + 1];
        gradeCount--;
    }
    rebuildLists();
    clampList();
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();
    ptmuInit();

    topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    botT = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuf = C2D_TextBufNew(8192);

    if (!loadData()) createSample();
    time_t nowT = time(NULL);
    ttDay = localtime(&nowT)->tm_wday;
    rebuildLists();

    bool is3d = false;
    bool dragging = false;
    bool quit = false;
    touchPosition t0 = {0}, tl = {0};

    while (aptMainLoop() && !quit) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        u32 up = hidKeysUp();
        frameNo++;

        bool act = (down & KEY_A) != 0;
        int moveY = ((down & KEY_DOWN) ? 1 : 0) - ((down & KEY_UP) ? 1 : 0);
        int moveX = ((down & KEY_RIGHT) ? 1 : 0) - ((down & KEY_LEFT) ? 1 : 0);
        int tabX = ((down & KEY_R) ? 1 : 0) - ((down & KEY_L) ? 1 : 0);
        bool back = (down & KEY_B) != 0;
        bool add = (down & KEY_X) != 0;
        bool del = (down & KEY_Y) != 0;

        if (down & KEY_START) break;

        // touch
        if (down & KEY_TOUCH) {
            hidTouchRead(&t0);
            tl = t0;
            dragging = false;
        }
        if (held & KEY_TOUCH) {
            hidTouchRead(&tl);
            if (abs((int)tl.px - (int)t0.px) > 10 || abs((int)tl.py - (int)t0.py) > 10) dragging = true;
        }
        if ((up & KEY_TOUCH) && !dragging) {
            int px = tl.px, py = tl.py;
            if (py >= 208) {
                if (px < 110) back = true;
                else if (px > 200) act = true;
            } else if (view == V_HUB) {
                int col = (px - 8) / 104, row = (py - 42) / 70;
                if (py >= 42 && col >= 0 && col < 3 && row >= 0 && row < 2) {
                    int i = row * 3 + col;
                    if (i == hubSel) act = true;
                    else hubSel = i;
                }
            } else if (py < 36 && px < 90) {
                back = true;
            } else if (view == V_TT && py >= 40 && py < 64) {
                int i = (px - 8) / 43;
                if (i >= 0 && i < 7) {
                    ttDay = (i + 1) % 7;
                    lsel = lfirst = 0;
                    rebuildLists();
                }
            } else if (view == V_ASG && py >= 40 && py < 64) {
                int i = (px - 8) / 102;
                if (i >= 0 && i < 3) {
                    filt = i;
                    lsel = lfirst = 0;
                    rebuildLists();
                }
            } else if (view == V_SET && py >= 44) {
                int row = (py - 44) / 26;
                if (row >= 0 && row < 6) {
                    if (row == setSel) act = true;
                    else setSel = row;
                }
            } else if (view != V_SET) {
                float top, h;
                int vis;
                listGeom(&top, &h, &vis);
                float step = (view == V_TT || view == V_ASG) ? 46 : (view == V_GRD ? 28 : 54);
                if (py >= top) {
                    int idx = lfirst + (int)((py - top) / step);
                    if (idx >= 0 && idx < listCount()) {
                        if (idx == lsel && view == V_ASG) act = true;
                        lsel = idx;
                    }
                }
            }
        }

        // buttons
        if (view == V_HUB) {
            if (moveX && hubSel + moveX >= 0 && hubSel + moveX < 6) hubSel += moveX;
            if (moveY && hubSel + moveY * 3 >= 0 && hubSel + moveY * 3 < 6) hubSel += moveY * 3;
            if (act) {
                static const int map[6] = {V_TT, V_ASG, V_GRD, V_CRS, V_LNK, V_SET};
                openView(map[hubSel]);
            } else if (back) {
                quit = true;
            }
        } else {
            if (back) {
                view = V_HUB;
                confirmFrame = 0;
            } else if (view == V_SET) {
                if (moveY && setSel + moveY >= 0 && setSel + moveY < 6) setSel += moveY;
                if (moveX) changeSetting(moveX, false);
                if (act) changeSetting(1, true);
            } else {
                if (moveY) {
                    lsel += moveY;
                    confirmFrame = 0;
                }
                if (view == V_TT && tabX) {
                    ttDay = (ttDay + tabX + 7) % 7;
                    lsel = lfirst = 0;
                } else if (view == V_ASG && tabX) {
                    filt = (filt + tabX + 3) % 3;
                    lsel = lfirst = 0;
                    confirmFrame = 0;
                }
                if (view == V_ASG && act && aCount) {
                    Assign *a = &asgs[aIdx[lsel]];
                    a->status = (u8)((a->status + 1) % 3);
                }
                if (view == V_ASG && add) promptAddAssignment();
                if (view == V_GRD && add) promptAddGrade();
                if ((view == V_ASG || view == V_GRD) && del) {
                    if (confirmFrame && frameNo - confirmFrame < 180) {
                        deleteSelected();
                        confirmFrame = 0;
                    } else {
                        confirmFrame = frameNo;
                    }
                }
                rebuildLists();
                clampList();
            }
        }

        // stereoscopic 3D only when the slider is up and depth is enabled
        g3d = osGet3DSliderState();
        bool want3d = g3d > 0.02f && depthMul[depthSet] > 0.0f;
        if (want3d != is3d) {
            gfxSet3D(want3d);
            is3d = want3d;
        }

        C2D_TextBufClear(textBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(topL, RGB(0, 0, 0));
        C2D_SceneBegin(topL);
        drawTop(0);
        if (is3d) {
            C2D_TargetClear(topR, RGB(0, 0, 0));
            C2D_SceneBegin(topR);
            drawTop(1);
        }
        C2D_TargetClear(botT, RGB(0, 0, 0));
        C2D_SceneBegin(botT);
        drawBottom();
        C3D_FrameEnd(0);
    }

    saveData();
    C2D_TextBufDelete(textBuf);
    ptmuExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
