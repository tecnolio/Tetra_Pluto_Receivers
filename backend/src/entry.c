#include "platform.h"
extern int main(int,char**);
extern int __libc_start_main(int (*)(int,char**,char**),int,char**,void*,void*,void*,void*);
static int entry_main(int argc,char**argv,char**env){(void)env;return main(argc,argv);}
void pt_entry(unsigned long *stack){__libc_start_main(entry_main,(int)stack[0],(char**)(stack+1),0,0,0,stack);for(;;){}}
