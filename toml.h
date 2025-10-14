/*
  MIT License

  Copyright (c) 2017 - 2021 CK Tan
  https://github.com/cktan/tomlc99

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
#ifndef TOML_H
#define TOML_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct toml_timestamp_t toml_timestamp_t;
    typedef struct toml_table_t toml_table_t;
    typedef struct toml_array_t toml_array_t;
    typedef struct toml_datum_t toml_datum_t;

    /* Parse a file. Return a table on success, or 0 otherwise.
     * Caller must toml_free(the-return-value) after use.
     */
    toml_table_t* toml_parse_file(FILE* fp, char* errbuf, int errbufsz);

    /* Free the table returned by toml_parse_file() or toml_parse(). */
    void toml_free(toml_table_t* tab);

    /* Timestamp types. The year, month, day, hour, minute, second, z
     * fields may be NULL if they are not relevant.
     */
    struct toml_timestamp_t
    {
        struct
        { /* internal. do not use. */
            int year, month, day;
            int hour, minute, second, millisec;
            char z[10];
        } __buffer;
        int *year, *month, *day;
        int *hour, *minute, *second, *millisec;
        char* z;
    };

    /* Enhanced access methods */
    struct toml_datum_t
    {
        int ok;
        union
        {
            toml_timestamp_t* ts; /* ts must be freed after use */
            char* s;              /* string value. s must be freed after use */
            int b;                /* bool value */
            int64_t i;            /* int value */
            double d;             /* double value */
        } u;
    };

    /* on arrays: */
    /* ... retrieve size of array. */
    int toml_array_nelem(const toml_array_t* arr);
    /* ... retrieve values using index. */
    toml_datum_t toml_string_at(const toml_array_t* arr, int idx);
    toml_datum_t toml_bool_at(const toml_array_t* arr, int idx);
    toml_datum_t toml_int_at(const toml_array_t* arr, int idx);
    toml_datum_t toml_double_at(const toml_array_t* arr, int idx);
    toml_datum_t toml_timestamp_at(const toml_array_t* arr, int idx);
    /* ... retrieve array or table using index. */
    toml_array_t* toml_array_at(const toml_array_t* arr, int idx);
    toml_table_t* toml_table_at(const toml_array_t* arr, int idx);

    /* on tables: */
    /* ... retrieve value using key. */
    toml_datum_t toml_string_in(const toml_table_t* arr, const char* key);
    toml_datum_t toml_bool_in(const toml_table_t* arr, const char* key);
    toml_datum_t toml_int_in(const toml_table_t* arr, const char* key);
    toml_datum_t toml_double_in(const toml_table_t* arr, const char* key);
    toml_datum_t toml_timestamp_in(const toml_table_t* arr, const char* key);
    /* ... retrieve array or table using key. */
    toml_array_t* toml_array_in(const toml_table_t* tab, const char* key);
    toml_table_t* toml_table_in(const toml_table_t* tab, const char* key);

    /*
     * Process raw string and int.
     * DEPRECATED: The use of these functions is discouraged.
     */
    int toml_rtos(char* s, char** ret);
    int toml_rtoi(const char* s, int64_t* ret);

    /*
     * Misc
     */
    int toml_key_exists(const toml_table_t* tab, const char* key);
    int toml_key_in(const toml_table_t* tab, const char* key);
    /* ... retrieve the key in table at keyidx. Return 0 if out of range. */
    const char* toml_key_in_table(const toml_table_t* tab, int keyidx);
    /* ... retrieve number of keys in table. */
    int toml_table_nkval(const toml_table_t* tab);
    /* ... retrieve number of arrays in table. */
    int toml_table_narr(const toml_table_t* tab);
    /* ... retrieve number of sub-tables in table. */
    int toml_table_ntab(const toml_table_t* tab);
    /* ... retrieve table in table at tabidx. Return 0 if out of range. */
    toml_table_t* toml_table_table(const toml_table_t* tab, int tabidx);

    /* misc */
    void toml_set_memutil(void* (*xxmalloc)(size_t), void (*xxfree)(void*));

#ifdef __cplusplus
}
#endif

#endif /* TOML_H */