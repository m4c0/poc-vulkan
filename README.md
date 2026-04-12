# poc-vulkan

In order to read and try any example from this repository, all you need is a
valid C compiler.

The code relies on Vulkan headers (added as submodule) and Volk for dynamically
loading.

If you want to mess around shaders, you also need a compiler like `glslang`
installed. But pre-compiled copies are added for convenience.

**DISCLAIMER:** this is not production-ready code. It "just works" and it is
mean for learning how specific Vulkan features work without diving into a world
of third-party dependencies or confusing API wrappers. Read more about it in
the "known issues" section

The focus of this repository is to experiment with different Vulkan resources.
If you want to use any of its examples in a more serious environment, do
**not** use the code as-is. Create your own infrastructure then feel free to
use this repository to reproduce issues, test ideas, etc. 

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

Also, it's entirely possible to build examples individually with a command line
like this:

```
clang -o hello hello.c -IVulkan-Headers/include
```

## Experimenting with this repo

Given how each example is independent from each other, it should be kinda easy
to pick one and play around. A couple of warnings though:

First, get Vulkan Validation Layer working. The code will just run without it,
but what happens when you don't pass validations is implementation-dependent.

## Known issues

These are intentional to keep the code simpler to study and experiment.

### Loads of copy-pasta

Other than using Volk and Vulkan headers, there is no code-sharing between
examples. You should be able to open a one file and understand everything it
requires with simple code navigation.

### Examples leak resources

If you see a bit or memory allocated by `malloc`, chances are you won't see a
corresponding `free`.

### Error handling with `assert`

This just works for examples. As long you never compile the code with `NDEBUG`
enabled, `assert` is a quick hacky way to test a condition, abort on error
while printing the failing code.

