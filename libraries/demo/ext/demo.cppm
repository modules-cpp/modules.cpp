module;

#include "demo-c.h"

export module lib.demo;

export namespace lib::demo {

int answer() {
    return demo_answer();
}

}
