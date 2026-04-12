# poc-vulkan

In order to read and try this example, all you need is a valid C compiler and
the current repo.

The code relies on Vulkan headers (added as submodule) and Volk for dynamically
loading.

If you want to mess around shaders, you also need a compiler like `glslang`
installed. But pre-compiled copies are added for convenience.

## Build

Using `clang` as example:

```sh
clang -o build build.c
./build
```

Then you should be able to run any example.

This should work on Windows by changing `-o build` to `-o build.exe`. The
builder adds the `exe` extension automatically.

Whilst not tested, this should work with different C compilers as well (GCC,
MSVC, etc).
