// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.rtc:types;

export namespace mm::rtc {

// A calendar, in the units a person reads. year is the full year rather than an
// offset from some base, because every offset convention is a trap for someone:
// a part that stores two digits is the provider's problem, not the caller's.
// month and day count from one; weekday counts from zero, where zero is the
// part's own first day and mm.rtc does not name which day that is.
struct DateTime {
    unsigned int year = 0;
    unsigned int month = 1;
    unsigned int day = 1;
    unsigned int weekday = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
};

}
