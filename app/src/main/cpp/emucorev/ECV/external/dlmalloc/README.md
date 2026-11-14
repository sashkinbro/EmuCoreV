# dlmalloc
A C++ version of Doug Lea's excellent malloc() implementation.

I have been having issues with the malloc() implementation on Windows, where the memory usage can grow unreasonably large with some patterns of successive reallocations. So I decided to investigate Doug Lea's [malloc implementation](http://g.oswego.edu/dl/html/malloc.html). It is very easy to use, a [single C file](ftp://g.oswego.edu/pub/misc/malloc.c) that you just compile and link along with your source code, and you are all set.

Using Doug's malloc.c worked beautifully. For my use case, it was faster than Windows default malloc (I'm using Visual C++ 2015), and the memory usage was considerably reduced.

I wanted to understand the code, as I would like to use its core logic to make a custom C++ allocator for my [sparse hash table](https://github.com/greg7mdp/sparsepp). However, I found the source code very difficult to follow. A lot of it is implemented with preprocessor macros, and is invisible to debuggers. Macros are of course necessary when coding in C for ultimate performance, but they sure obfuscate the code.

Also, I'd like my C++ allocator to be header only, but shoving all these macro definitions into a header file would pollute the global name space, which is not acceptable.

Therefore I decided to convert Doug's original malloc.c (v2.8.6) to C++, and this is what you find here. I have not modified what the code does, just how it is specified and organized (modulo any typo or bug I may have introduced of course).

Interestingly, the C++ version is just as fast as the original C version! In my tests (Visual C++ 2015, Windows 10), the C++ version was even very slightly faster.

While Linus may be opposed to C++, I still though that sharing this C++ implementation could be useful for people who, like me, would like to examine the inner workings of one of the best malloc implementation ever written.

### License

Both the original source code from Doug Lea, and my changes are released to the public domain, as explained at http://creativecommons.org/publicdomain/zero/1.0/.
