module;

#include "cdemo-c.h"

export module lib.cdemo;

export namespace lib::cdemo {

int scale(int value) {
    return cdemo_scale(value);
}

int twice(int value) {
    return mm_cdemo_twice(value);
}

}
