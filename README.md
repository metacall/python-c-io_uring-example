# [MetaCall](https://github.com/metacall/core) Python C `io_uring` Example

[`io_uring`](https://en.wikipedia.org/wiki/Io_uring) is a new Linux Kernel interface that speeds up I/O operations in comparison to previous implementations like [`libuv`](https://libuv.org/) (the library that NodeJS uses internally for handling I/O).

This interface is offered through [`liburing`](https://github.com/axboe/liburing), which provides a C API for accessing it. We could write a Python extension by using [`Python C API`](https://docs.python.org/3/c-api/index.html), or use [`ctypes`](https://docs.python.org/3/library/ctypes.html) in order to call to the library.

Developing those wrappers is costly because either we have to write C/C++ or Python boilerplate. So instead of doing that, we will be using [MetaCall](https://github.com/metacall/core) in order to achieve this.

MetaCall allows to transparently call to C functions, we can implement anything in C and without need to compile it manually (it will be JITed at load time). So basically we can load C code directly into Python. For example:

```c
int sum(int a, int b) {
	return a + b;
}
```

```py
from sum.c import sum

sum(3, 4) # 7
```

With this we can use our C library not only from Python but from any other language, for example, NodeJS:

```js
const { sum } = require("./sum.c");

sum(3, 4) // 7
```

We will be avoiding all the boilerplate and we will have a single interface for all languages. The calls will be also type safe and we will avoid a lot of errors and time for maintaining the wrappers for each language that we can spend focusing on the development. Here's an example of [Cython + LWAN](https://www.nexedi.com/NXD-Blog.Multicore.Python.HTTP.Server), as you can see, although the performance is excellent, the boilerplate is huge in comparison to MetaCall.

In this example we want to bring the power of `io_uring` to Python for maximizing the speed of I/O and outperform Python native primitives like `http.server` module. For demonstrating it, we have a `server_listen` function which creates a simple HTTP server in the port `8000`. As example, we are also passing another parameter as a callback called `fibonacci`. This is a function that will act as a handler for the POST method, we will use it for calculating the fibonacci sequence. When doing a POST against the server, we will use the path as the Nth element of fibonacci sequence to be calculated. In our C code this will get translated automatically into a function pointer with the signature `long (*handler)(long)` transparently. Invoking the callback `handler` from C land will execute the function `fibonacci` in Python land.

## Docker

Building and running with Docker:

```bash
docker build -t metacall/python-c-io_uring-example .
docker run --rm -p 8000:8000 -it metacall/python-c-io_uring-example
```

## Testing

For calculating the Fibonacci of 15 and 17 respectively:

```bash
curl -X POST http://localhost:8000/15
610
curl -X POST http://localhost:8000/17
1597
```
