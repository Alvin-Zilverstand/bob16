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
#define BOB64_BOOT_PATH "build\\bob64\\EFI\\BOOT\\BOOTX64.EFI"
#define BOB64_HANDOFF_BOOT_PATH "build\\bob64-handoff\\EFI\\BOOT\\BOOTX64.EFI"
#define BOB_COMMAND "bob.exe"
#define BOB32_COMMAND "bob32.exe"
#else
#define EXE ""
#define BUILD_DIR "./build/"
#define BOB64_BOOT_PATH "build/bob64/EFI/BOOT/BOOTX64.EFI"
#define BOB64_HANDOFF_BOOT_PATH "build/bob64-handoff/EFI/BOOT/BOOTX64.EFI"
#define BOB_COMMAND "./bob"
#define BOB32_COMMAND "./bob32"
#endif

static void run(const char *command) {
    if(system(command)!=0){fprintf(stderr,"Build failed: %s\n",command);exit(1);}
}
static void ensure_directory(const char *path) {
#ifdef _WIN32
    if(_mkdir(path) && errno!=EEXIST){perror(path);exit(1);}
#else
    if(mkdir(path,0777) && errno!=EEXIST){perror(path);exit(1);}
#endif
}
static void build_bob64(int handoff) {
    const char *compiler=getenv("BOB64_CC");
    const char *output_path=handoff?BOB64_HANDOFF_BOOT_PATH:BOB64_BOOT_PATH;
    const char *handoff_define=handoff?"-DBOB64_ENABLE_HANDOFF ":"";
    char command[2048];
    ensure_directory("build");
    ensure_directory("build/bob64-app");
    run("gcc -std=c11 -O2 -Wall -Wextra -Werror tools/bob64cc.c -o build/bob64cc" EXE);
    run(BUILD_DIR "bob64cc" EXE " apps/bob64_smoke.c build/bob64-app/smoke.b64e");
    run(BUILD_DIR "bob64cc" EXE " apps/bob64_display.c build/bob64-app/display.b64e");
    run(BUILD_DIR "bob64cc" EXE " apps/bob64_gui.c build/bob64-app/gui.b64e");
    ensure_directory(handoff?"build/bob64-handoff":"build/bob64");
    if(handoff) {
        ensure_directory("build/bob64-handoff/EFI");
        ensure_directory("build/bob64-handoff/EFI/BOOT");
    } else {
        ensure_directory("build/bob64/EFI");
        ensure_directory("build/bob64/EFI/BOOT");
    }
    if(!compiler || !*compiler) {
#ifdef _WIN32
        compiler="gcc";
#else
        compiler="x86_64-w64-mingw32-gcc";
#endif
    }
    if(snprintf(command,sizeof(command),"%s -std=c11 -O2 -Wall -Wextra -Werror -DBOB64_UEFI_ABI %s-ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -nostdlib -Wl,--subsystem,10 -Wl,-e,efi_main -Wl,--image-base,0x100000 -Wl,--enable-reloc-section -Wl,--dynamicbase -Wl,--file-alignment,0x200 -Wl,--section-alignment,0x1000 bob64/boot.c bob64/memory.c bob64/paging.c bob64/cpu.c bob64/descriptors.c bob64/interrupts.c bob64/interrupts.S bob64/syscall.c bob64/window_server.c bob64/bootstrap.c bob64/kernel.c bob64/console.c bob64/heap.c bob64/keyboard.c bob64/mouse.c bob64/shell.c bob64/filesystem.c bob64/snapshot.c bob64/exec.c bob64/process.c bob64/compiler.c bob64/embedded_app.S bob64/handoff.S -o %s",compiler,handoff_define,output_path)>=(int)sizeof(command)) {
        fprintf(stderr,"bob64 compiler command is too long\n");exit(1);
    }
    run(command);
}
int main(int argc,char **argv) {
    int bob64Handoff=argc==2&&(!strcmp(argv[1],"--bob64-handoff")||!strcmp(argv[1],"--bob64-handoff-test"));
    int bob64Only=argc==2&&(!strcmp(argv[1],"--bob64")||!strcmp(argv[1],"--bob64-test")||bob64Handoff);
    int bob64Test=argc==2&&(!strcmp(argv[1],"--bob64-test")||!strcmp(argv[1],"--bob64-handoff-test"));
    if(argc>2||(argc==2&&!bob64Only&&strcmp(argv[1],"--run")&&strcmp(argv[1],"--run32")&&strcmp(argv[1],"--test"))) {
        fprintf(stderr,"Usage: build [--run | --run32 | --test | --bob64 | --bob64-test | --bob64-handoff | --bob64-handoff-test] (from repository root)\n");return 1;
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
    if(bob64Only) {
        build_bob64(bob64Handoff);
        if(bob64Test) {
            run("gcc -std=c11 -O2 -Wall -Wextra -Werror tests/test_bob64.c bob64/libc.c bob64/compiler.c bob64/memory.c bob64/paging.c bob64/cpu.c bob64/descriptors.c bob64/interrupts.c bob64/interrupts.S bob64/syscall.c bob64/window_server.c bob64/bootstrap.c bob64/console.c bob64/heap.c bob64/keyboard.c bob64/mouse.c bob64/shell.c bob64/filesystem.c bob64/snapshot.c bob64/exec.c bob64/process.c -o build/test_bob64" EXE);
            if(bob64Handoff) run(BUILD_DIR "test_bob64" EXE " " BOB64_HANDOFF_BOOT_PATH " build/bob64-app/smoke.b64e build/bob64-app/display.b64e build/bob64-app/gui.b64e");
            else run(BUILD_DIR "test_bob64" EXE " " BOB64_BOOT_PATH " build/bob64-app/smoke.b64e build/bob64-app/display.b64e build/bob64-app/gui.b64e");
        }
        return 0;
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
