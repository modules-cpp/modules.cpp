module;

#include "demo-c.h"

export module ext.demo;

export namespace ext::demo {

int answer() {
    return demo_answer();
}

}
