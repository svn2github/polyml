/*
    Title:  Bitmap.  Generally used by the garbage collector to indicate allocated words

    Copyright (c) 2006, 2012  David C.J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef BITMAP_H_DEFINED
#define BITMAP_H_DEFINED

#include "globals.h" // For POLYUNSIGNED

class Bitmap
{
public:
    Bitmap(): m_bits(0) {}
    ~Bitmap();

    bool Create(POLYUNSIGNED bits);

private:
    static unsigned char BitN(POLYUNSIGNED n) { return 1 << (n & 7); }
public:
    // Set a single bit
    void SetBit(POLYUNSIGNED n) { m_bits[n >> 3] |=  BitN(n); }
    // Set a range of bits
    void SetBits(POLYUNSIGNED bitno, POLYUNSIGNED length);
    // Clear a range of bits.  May already be partly clear
    void ClearBits(POLYUNSIGNED bitno, POLYUNSIGNED length);
    // Test a bit
    bool TestBit(POLYUNSIGNED n) const { return (m_bits[n >> 3] & BitN(n)) != 0; }
    // How many zero bits (maximum n) are there in the bitmap, starting at location start?
    POLYUNSIGNED CountZeroBits(POLYUNSIGNED bitno, POLYUNSIGNED n);
    //* search the bitmap from the high end down looking for n contiguous zeros
    POLYUNSIGNED FindFree(POLYUNSIGNED limit, POLYUNSIGNED bitno, POLYUNSIGNED n);
private:

    unsigned char *m_bits;
};

// A wrapper class that adds the address range.  It is used when scanning
// memory to see if an address has already been visited.
class VisitBitmap: public Bitmap
{
public:
    VisitBitmap(PolyWord *bottom, PolyWord *top):
        m_bottom(bottom), m_top(top) { (void)Create(top - bottom); }

    bool AlreadyVisited(PolyObject *p)   { return TestBit((PolyWord*)p - m_bottom); }
    void SetVisited(PolyObject *p)       { SetBit((PolyWord*)p - m_bottom); }
    bool InRange(PolyWord *p)            { return p >= m_bottom && p < m_top; }

protected:
    PolyWord  *m_bottom;
    PolyWord  *m_top;
};

#endif


