/*

MIT License

Copyright (c) 2026 Shunichi Kitahara

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef _MOCKINGBOARD_H
#define _MOCKINGBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "via6522.h"

// Flat 4 VIA mapping:
// vias[0] = MockingBoard #0 VIA #0 ($Cn00-$Cn0F)
// vias[1] = MockingBoard #0 VIA #1 ($Cn80-$Cn8F)
// vias[2] = MockingBoard #1 VIA #0 ($Cn00-$Cn0F)
// vias[3] = MockingBoard #1 VIA #1 ($Cn80-$Cn8F)

#endif // _MOCKINGBOARD_H
