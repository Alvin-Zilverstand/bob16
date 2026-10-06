/* Run from the repository root. Build tools, emulator and guest without Python. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define EXE ".exe"
#define BUILD_DIR "build\\"
#define BOB_COMMAND "bob.exe"
#else
#include <sys/stat.h>
#define EXE ""
#define BUILD_DIR "./build/"
#define BOB_COMMAND "./bob"
#endif

static void run(const char *command) {
    if(system(command)!=0){fprintf(stderr,"Build failed: %s\n",command);exit(1);}
}
int main(int argc,char **argv) {
    if(argc>2||(argc==2&&strcmp(argv[1],"--run")&&strcmp(argv[1],"--test"))) {
        fprintf(stderr,"Usage: build [--run | --test] (from repository root)\n");return 1;
    }
#ifdef _WIN32
    _mkdir("build");
    FILE *gcc=fopen("C:/msys64/ucrt64/bin/gcc.exe","rb");
    if(gcc){fclose(gcc);const char *path=getenv("PATH");size_t size=(path?strlen(path):0)+64;
        char *value=malloc(size);if(!value)return 1;snprintf(value,size,"C:/msys64/ucrt64/bin;%s",path?path:"");_putenv_s("PATH",value);free(value);}
#else
    mkdir("build",0777);
#endif
    run("gcc -std=gnu11 -O2 -Wall -Wextra -Werror main.c -o bob" EXE);
    run("gcc -std=c11 -O2 -Wall -Wextra -Werror tools/bobcc.c -o build/bobcc" EXE);
    run("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel kernel/kernel.c -o build/kernel.i");
    run(BUILD_DIR "bobcc" EXE " build/kernel.i kernel.basm build/kernel.b16");
    if(argc==2&&!strcmp(argv[1],"--test")) {
        run("gcc -std=c11 -O2 -Wall -Wextra -Werror tests/test_os.c -o build/test_os" EXE);
        run(BUILD_DIR "test_os" EXE);
    } else if(argc==2) run(BOB_COMMAND " --boot build/kernel.b16");
    return 0;
}
