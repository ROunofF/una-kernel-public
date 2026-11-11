#include "Components/Misc/utiltime.hpp"

std::string Misc::Time::time2str(time_t t)
{
    struct tm tm;
    char      timeStart[50];

    gmtime_r(&t, &tm);

    sprintf(timeStart, "%04d/%02d/%02d %02d:%02d:%02d", tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec);

    std::string s(timeStart);

    return s;
}

uint32_t Misc::Time::time2seconds(const char* str)
{
    int p1 = 0;
    int p2 = 0;
    int p3 = 0;

    int count = sscanf(str, "%d:%d:%d", &p1, &p2, &p3);

    int hh = 0;
    int mm = 0;
    int ss = 0;

    if (count == 1) {
        ss = p1;
    } else if (count == 2) {
        mm = p1;
        ss = p2;
    } else if (count == 3) {
        hh = p1;
        mm = p2;
        ss = p3;
    }

    return hh * 3600 + mm * 60 + ss;
}

time_t Misc::Time::makeTime(const char* str)
{
    int y;
    int m;
    int d;
    int hh;
    int mm;
    int ss;

    int count = sscanf(str, "%d/%d/%d %d:%d:%d", &y, &m, &d, &hh, &mm, &ss);

    if (count != 6) {
        return 0;
    }

    return makeTime(y, m, d, hh, mm, ss);
}

time_t Misc::Time::makeTime(int y, int m, int d, int hh, int mm, int ss)
{
    struct tm info {};

    info.tm_year = y - 1900;
    info.tm_mon  = m - 1;
    info.tm_mday = d;
    info.tm_hour = hh;
    info.tm_min  = mm;
    info.tm_sec  = ss;

    return mktime(&info);
}
