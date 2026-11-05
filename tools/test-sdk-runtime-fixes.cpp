// Regression coverage for the BMP decoder and CUtlString fixes from SDK PR #2171.
// Run tools/test-sdk-runtime-fixes.ps1 after building the Windows mod.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "imageutils.h"
#include "tier1/utlstring.h"

// The imageutils VTF conversion helper references this client interface;
// BMP decoding uses GDI and does not require an engine or filesystem instance.
class IVEngineClient;
IVEngineClient *engine = NULL;

static void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void TestBitmap(const char *directory, int bits, int width)
{
    const int height = 3;
    const int colors = bits <= 8 ? 1 << bits : 0;
    const int stride = ((width * bits + 31) / 32) * 4;
    std::vector<RGBQUAD> palette(colors);
    for (int i = 0; i < colors; ++i)
    {
        palette[i].rgbRed = (i * 71 + 39) & 255;
        palette[i].rgbGreen = (i * 53 + 83) & 255;
        palette[i].rgbBlue = (i * 29 + 161) & 255;
        palette[i].rgbReserved = 0;
    }
    std::vector<unsigned char> pixels(stride * height, 0);
    std::vector<unsigned char> expected(width * height * 4, 255);
    for (int y = 0; y < height; ++y)
    {
        unsigned char *row = &pixels[(height - 1 - y) * stride];
        for (int x = 0; x < width; ++x)
        {
            unsigned char *rgba = &expected[(y * width + x) * 4];
            if (colors)
            {
                const int index = (x + y * 3) % colors;
                rgba[0] = palette[index].rgbRed;
                rgba[1] = palette[index].rgbGreen;
                rgba[2] = palette[index].rgbBlue;
                if (bits == 1) row[x / 8] |= index << (7 - x % 8);
                else if (bits == 4) row[x / 2] |= index << (x % 2 ? 0 : 4);
                else row[x] = index;
            }
            else
            {
                rgba[0] = (x * 71 + y * 13 + 39) & 255;
                rgba[1] = (x * 53 + y * 29 + 83) & 255;
                rgba[2] = (x * 29 + y * 71 + 161) & 255;
                row[x * (bits / 8)] = rgba[2];
                row[x * (bits / 8) + 1] = rgba[1];
                row[x * (bits / 8) + 2] = rgba[0];
                if (bits == 32) row[x * 4 + 3] = rgba[3] = 63 + x;
            }
        }
    }
    BITMAPFILEHEADER fileHeader = {};
    BITMAPINFOHEADER info = {};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(info) + colors * sizeof(RGBQUAD);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixels.size());
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = static_cast<WORD>(bits);
    info.biSizeImage = static_cast<DWORD>(pixels.size());
    info.biClrUsed = colors;
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%s/bmp-%d-%d.bmp", directory, bits, width);
    FILE *file = std::fopen(path, "wb");
    Require(file != NULL, "create BMP fixture");
    std::fwrite(&fileHeader, sizeof(fileHeader), 1, file);
    std::fwrite(&info, sizeof(info), 1, file);
    if (colors) std::fwrite(&palette[0], sizeof(RGBQUAD), colors, file);
    std::fwrite(&pixels[0], 1, pixels.size(), file);
    Require(std::fclose(file) == 0, "write BMP fixture");

    int actualWidth = 0, actualHeight = 0;
    ConversionErrorType error = CE_ERROR_PARSING_SOURCE;
    unsigned char *actual = ImgUtl_ReadBMPAsRGBA(path, actualWidth, actualHeight, error);
    if (!actual) std::fprintf(stderr, "Decoder error: %d\n", error);
    Require(actual && error == CE_SUCCESS, path);
    Require(actualWidth == width && actualHeight == height, "decoded dimensions");
    for (size_t i = 0; i < expected.size(); ++i)
        if (actual[i] != expected[i])
        {
            std::fprintf(stderr, "Byte %zu: expected %u, got %u\n", i, expected[i], actual[i]);
            Require(false, path);
        }
    MemAlloc_Free(actual);
    std::printf("PASS: %d-bit BMP, %dx%d (palette, row padding, orientation, RGBA)\n", bits, width, height);
}

static void TestStrings()
{
    const char *inputs[] = { "", "abc", " ", "   ", " abc", "   abcdefghijklmnopqrstuvwxyz", "  a b " };
    const char *outputs[] = { "", "abc", "", "", "abc", "abcdefghijklmnopqrstuvwxyz", "a b " };
    for (int i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
    {
        CUtlString single(inputs[i]), multiple(inputs[i]);
        single.TrimLeft(' ');
        multiple.TrimLeft(" \t\r\n");
        Require(std::strcmp(single.String(), outputs[i]) == 0, "TrimLeft(char)");
        Require(std::strcmp(multiple.String(), outputs[i]) == 0, "TrimLeft(string)");
    }
    CUtlString mixed("\t \r\n a b\t");
    mixed.TrimLeft(" \t\r\n");
    Require(std::strcmp(mixed.String(), "a b\t") == 0, "mixed leading whitespace");
    std::puts("PASS: both TrimLeft overloads, overlap, empty and whitespace-only strings");
}

int main(int argc, char **argv)
{
    Require(argc == 2, "usage: test-sdk-runtime-fixes <existing fixture directory>");
    const int bits[] = { 4, 1, 8, 24, 32 };
    const int widths[] = { 1, 2, 3, 7, 8, 9, 17 };
    for (int b : bits)
        for (int w : widths)
            TestBitmap(argv[1], b, w);
    TestStrings();
    return 0;
}
