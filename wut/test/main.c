#include "wut.h"

#include <stdio.h>

/* Do not modify this function, you should call this to check for any value
   you want to inspect from the solution. */
void check(int value, const char* message) {
    printf("Check: %d (%s)\n", value, message);
}

int id[3] = {0};
int count = 0;

void run(void){
   int current_id = wut_id();
   if (current_id % 2 != 0) {
     wut_create(run);
     return;
   }
   if (count == 3) return;
   id[count] = current_id;
   count++;
}

int main() {
    /*
    You may write any of your own testing code here.

    You can execute it using `build/test/wut`. However, without your own
    implementation, it shouldn't do much. We'll run this code with the solution
    so you can clarify anything you want, or write a tricky test case.

    Place at least one call to `check` with a value (probably a return from a
    library call) that you'd like to see the output of. For example, here's
    how to convert `tests/main-thread-is-0.c` into this format:
    
    wut_init();
    check(wut_id(), "wut_id of the main thread is should be 0");



    wut_init();
    /* Insert your code here, and remove this comment. */
    wut_init();
    check(count, "the count should be 0 at this point");
    wut_create(run);
    check(id[0], "the value of id[0] should be 2 at this point");
    check(count, "the value of count should be 1 at this point");

    return 0;
}
