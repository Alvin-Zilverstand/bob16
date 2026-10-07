/* Run from the repository root. Build tools, emulator and guest without Python. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define EXE ".exe"
#define BUILD_DIR "build\\"
#define BOB_COMMAND "bob.exe"
#define BOB32_COMMAND "bob32.exe"
#else
#define EXE ""
#define BUILD_DIR "./build/"
#define BOB_COMMAND "./bob"
#define BOB32_COMMAND "./bob32"
#endif

static void run(const char *command) {
    if(system(command)!=0){fprintf(stderr,"Build failed: %s\n",command);exit(1);}
}
int main(int argc,char **argv) {
    if(argc>2||(argc==2&&strcmp(argv[1],"--run")&&strcmp(argv[1],"--run32")&&strcmp(argv[1],"--test"))) {
        fprintf(stderr,"Usage: build [--run | --run32 | --test] (from repository root)\n");return 1;
    }
#ifdef _WIN32
    int directory_result=_mkdir("build");
#else
    int directory_result=mkdir("build",0777);
#endif
    if(directory_result && errno!=EEXIST){perror("build");return 1;}
    struct stat directory;
    if(stat("build",&directory)) {perror("build");return 1;}
#ifdef _WIN32
    int is_directory=(directory.st_mode&_S_IFMT)==_S_IFDIR;
#else
    int is_directory=S_ISDIR(directory.st_mode);
#endif
    if(!is_directory){
        fprintf(stderr,"build must be a directory\n");return 1;
    }
    run("gcc -std=gnu11 -O2 -Wall -Wextra -Werror main.c -o bob" EXE);
    run("gcc -std=gnu11 -O2 -Wall -Wextra -Werror main.c -o bob32" EXE);
    run("gcc -std=c11 -O2 -Wall -Wextra -Werror tools/bobcc.c -o build/bobcc" EXE);
    run("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -I kernel kernel/kernel.c -o build/kernel.i");
    run(BUILD_DIR "bobcc" EXE " build/kernel.i kernel.basm build/kernel.b16");
    run("gcc -E -P -nostdinc -undef -DBOBC_LEGACY=0 -DBOBC_WIDE=1 -I kernel kernel/kernel.c -o build/kernel32.i");
    run(BUILD_DIR "bobcc" EXE " build/kernel32.i build/kernel32.basm build/kernel32.b32 --wide-kernel");
    if(argc==2&&!strcmp(argv[1],"--test")) {
        run("gcc -std=c11 -O2 -Wall -Wextra -Werror tests/test_cpu.c -o build/test_cpu" EXE);
        run(BUILD_DIR "test_cpu" EXE);
        run("gcc -std=c11 -O2 -Wall -Wextra -Werror tests/test_os.c -o build/test_os" EXE);
        run(BUILD_DIR "test_os" EXE);
#ifdef _WIN32
        run("gcc -std=c11 -O2 -Wall -Wextra -Werror tests/test_console.c -o build/test_console.exe");
        run("build\\test_console.exe");
#endif
    } else if(argc==2&&!strcmp(argv[1],"--run32")) run(BOB32_COMMAND " --boot build/kernel32.b32");
    else if(argc==2) run(BOB_COMMAND " --boot build/kernel.b16");
    return 0;
}
