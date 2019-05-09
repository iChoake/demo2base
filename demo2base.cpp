// demo2base.cpp : Extract a zlib-compressed City of Heroes supergroup BASE deifnition
//                 from a .demorecord formatted file.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdint.h>
#include <cassert>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>
#include <algorithm>

#include <zlib125.h>

using namespace std;

#define CHUNK 16384

// base output file path
string g_sgbaseTextFile;


// Unescape BASE data to prepare for passing to zlib inflate
bool unescape_base(string& in, ostream& out)
{
    string::iterator pos(in.begin());
    string::iterator end(in.end());
    ostreambuf_iterator<char> outpos(out);
    // convert first 4 bytes as little-endian length of uncompressed data
    int header_count = 4;
    uint32_t datalen = 0;
    // stat counters
    int icount = 0;
    int ocount = 0;

    // Start after initial quote, and read until end quote
    assert(*pos == '"');
    pos++;

    while (pos != end && *pos != '"')
    {
        byte c = *pos++; icount++;

        if (c == '\\' && pos != end)
        {
            //Advance iter again if a valid escape
            switch (*pos)
            {
            case 'n': c = '\n'; pos++; icount++; break;
            case 'r': c = '\r'; pos++; icount++; break;
            case '0': c = '\0'; pos++; icount++; break;
            case 't': c = '\t'; pos++; icount++; break;
            case 'q': c = '"';  pos++; icount++; break;
            case 's': c = '\''; pos++; icount++; break;
            case 'd': c = '$';  pos++; icount++; break;
            case 'p': c = '%';  pos++; icount++; break;
            case '\\' : c = '\\'; pos++; icount++; break;

            default: 
            // pass through, we should call it an error though (unknown escape pair)
                ;
            }
        }

        if (header_count != 0)
        {
            // The first 4 bytes is the uncompressed data length in little-endian order
            header_count--;
            datalen = (datalen >> 8) | (c << 24);
        }
        else
        {
            *outpos++ = c; ocount++;
        }
    }

    // Debugging
    cout << "demo2base: read " << icount << " escaped bytes" << endl;
    cout << "demo2base: inflating " << ocount << " unescaped bytes" << endl;
    cout << "demo2base: inflated length should be " << datalen << " bytes" << endl;

    return true;
}


// Debugging dump of the unescaped data ready for zlib inflating
bool dump_unescaped_base(string& data, const string& temppath)
{
    int len = data.length();
    char fmtbuf[24];

    sprintf(fmtbuf, "%02x %02x %02x %02x", byte(data[0]), byte(data[1]), byte(data[2]), byte(data[3]));
    cout << "demo2base: first 4 unescaped bytes: " << fmtbuf << endl;
    sprintf(fmtbuf, "%02x %02x %02x %02x", byte(data[len-4]), byte(data[len-3]), byte(data[len-2]), byte(data[len-1]));
    cout << "demo2base: last 4 unescaped bytes: " << fmtbuf << endl;

    ofstream tempfile(temppath, ios::binary);
    if (tempfile.fail())
    {
        cerr << "demo2base: could not open unescaped data dump for debugging" << endl;
        return false;
    }
    copy(data.begin(), data.end(), ostreambuf_iterator<char>(tempfile));
    tempfile.close();

    return true;
}


// Decompress from file source to file dest until stream ends or EOF.
// inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
// allocated for processing, Z_DATA_ERROR if the deflate data is
// invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
// the version of the library linked do not match, or Z_ERRNO if there
// is an error reading or writing the files. */
int inf(stringstream& source, FILE *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    byte in[CHUNK];
    byte out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, MAX_WBITS);
    if (ret != Z_OK)
        return ret;

    // Decompress until deflate stream ends or end of file
    do {
        source.read((char *)in, CHUNK);
        strm.avail_in = (unsigned int)source.gcount();
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        // Run inflate() on input until output buffer not full
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                // Display the zlib msg here
                if (strm.msg != NULL)
                {
                    cerr << "demo2base: zlib: " << strm.msg << endl;
                }
                return ret;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

        // Done when inflate() says it's done
    } while (ret != Z_STREAM_END);

    // Clean up and return
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


// Report a zlib or i/o error
void zerr(int ret)
{
    fputs("demo2base: ", stderr);
    switch (ret) {
    case Z_ERRNO:
            fputs("error writing decompressed output\n", stderr);
        break;
    case Z_STREAM_ERROR:
        fputs("invalid compression level\n", stderr);
        break;
    case Z_DATA_ERROR:
        fputs("invalid or incomplete deflate data\n", stderr);
        break;
    case Z_MEM_ERROR:
        fputs("out of memory\n", stderr);
        break;
    case Z_VERSION_ERROR:
        fputs("zlib version mismatch!\n", stderr);
    }
}


// Parse the cohdemo looking for a BASE definition
errno_t parse_demo(istream& in)
{
    string source, target, cmd, data;
    string line, empty;
    stringstream ss;
    streamoff offset = 0;

    while (getline(in, line), !in.eof())
    {
        // Read and parse one line at a time
        // Each line is: <source> <target> <cmd> <remaining data>
        // All fields are strings; even source and target are sometimes not numeric Ids (e.g. SKYFILE)
        // Abosrb any whitespace between the source, target and cmd
        // but the remainder is all characters until newline
        // (except whitespace immediately following cmd, which ws will absorb)
        ss << line;
        ss >> source >> target >> cmd >> ws;
        unsigned int pos = (unsigned int)ss.tellg();
        data = (pos > line.length() ? empty : line.substr((unsigned int)ss.tellg()));
        //cout << source << " " << target << " " << cmd << " " << "len=" << data.length() << "offset=" << offset << endl;
        ss.clear();
        ss.seekg(0);
        ss.seekp(0);

        if (_stricmp(cmd.c_str(), "base") == 0)
        {
            stringstream unescaped_data;
            if (!unescape_base(data, unescaped_data))
            {
                cerr << "demo2base: could not unescape supergroup BASE data at offset=" << offset << endl;
                return false;
            }
            unescaped_data.seekg(0);

            // Temp debugging to dump the intermediate input to zlib inflate
            dump_unescaped_base(unescaped_data.str(), "tempdump.txt");
            unescaped_data.seekg(0);

            // Open the output file for the final supergroup text data
            FILE *outfile = NULL;
            errno_t err = fopen_s(&outfile, g_sgbaseTextFile.c_str(), "wb");
            if (err != 0)
            {
                cerr << "demo2base: could not open output file: " << g_sgbaseTextFile << endl;
                return err;
            }

            // Perform the zlib inflate
            int ret = inf(unescaped_data, outfile);
            fclose(outfile);
            outfile = NULL;
            if (ret != Z_OK)
            {
                zerr(ret);
                return ret;
            }

            // For now, just stop once we process a BASE
            return ret;
        }

        offset = in.tellg();
    }

    return 0;
}


int _tmain(int argc, _TCHAR* argv[])
{
    if (argc != 3)
    {
        // usage error
        cerr << "demo2base usage: demo2base <demofile_in> <sgbasefile_out>" << endl;
        return 1;
    }

    string demoFile(argv[1]);
    g_sgbaseTextFile = argv[2];

    //Open input demorecord file
    ifstream demo(demoFile, ios::binary);
    if (demo.fail())
    {
        cerr << "demo2base: could not open input demorecord file: " << demoFile << endl;
        return -1;
    }

    errno_t ret = parse_demo(demo);

    return ret;
}
