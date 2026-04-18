#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#include <process.h>
#else
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define EXE(X) X ".exe"
#else
#define EXE(X) X
#endif

#ifdef __clang__
#  define CC "clang"
#elif _MSC_VER
#  define CC "cl"
#else
#  define CC "cc"
#endif

static void usage() {
  fprintf(stderr, "just call 'build' without arguments\n");
}

static int run(char ** args) {
  assert(args && args[0]);

#ifdef _WIN32
  if (0 == _spawnvp(_P_WAIT, args[0], (const char * const *)args)) {
    return 0;
  }
#else
  pid_t pid = fork();
  if (pid == 0) {
    execvp(args[0], args);
    abort();
  } else if (pid > 0) {
    int sl = 0;
    assert(0 <= waitpid(pid, &sl, 0));
    if (WIFEXITED(sl)) return WEXITSTATUS(sl);
  }
#endif

  fprintf(stderr, "failed to run child process: %s\n", args[0]);
  return 1;
}
static uint64_t mtime(const char * name) {
#ifdef _WIN32
  struct __stat64 s = {0};
  _stat64(name, &s);
  return s.st_mtime;
#else
  struct stat t;
  if (0 != stat(name, &t)) return 0;
  return t.st_mtimespec.tv_sec * 1000ULL + t.st_mtimespec.tv_nsec / 1000000;
#endif
}

static int shader(char * name) {
  char spv[1024];
  sprintf(spv, "%s.spv", name);

  char * args[] = { EXE("glslang"), "--target-env", "vulkan1.0", name, "-o", spv, 0 };
  return (mtime(name) >= mtime(spv)) && run(args);
}
static int compile_shaders() {
  if (shader("empty.comp")) {
    fprintf(stderr, "glslang is missing or failing to build empty example - shader compilation disabled\n");
    return 0;
  }

  if (shader("hello.comp")) return 1;
  if (shader("reduce.comp")) return 1;
  if (shader("query.comp")) return 1;
  return 0;
}

#define example(X) { \
  char * args[] = { EXE(CC), "-Wall", "-g", "-IVulkan-Headers/include", "-o", EXE(X), X ".c", 0 }; \
  if (mtime(X) >= mtime(EXE(X)) && run(args)) return 1; }
static int compile_examples() {
  example("hello");
  example("info");
  example("reduce");
  example("query");
  return 0;
}

int main(int argc, char ** argv) {
  if (argc != 1) return (usage(), 1);

  if (compile_shaders()) return 1;
  if (compile_examples()) return 1;

  return 0;
}
