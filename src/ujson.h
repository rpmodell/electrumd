/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of the  nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef __UJSON_H__
#define __UJSON_H__

#include <stdio.h>
#include <stddef.h>

#define JSONOBJ_IS_STRING(obj) (obj->type == STRING)
#define JSONOBJ_IS_BOOL(obj) (obj->type == BOOL)
#define JSONOBJ_IS_INT(obj) (obj->type == INT)
#define JSONOBJ_IS_DOUBLE(obj) (obj->type == DOUBLE)
#define JSONOBJ_IS_LIST(obj) (obj->type == LIST)
#define JSONOBJ_IS_OBJECT(obj) (obj->type == JSON_OBJ)
#define JSONOBJ_IS_NULL(obj) (obj->type == JSON_NULL)

#define JSONOBJ_LIST_SIZE(L) ((L)->e.list_value.size)
#define JSONOBJ_FOREACH(list, obj) for (obj = list->child; obj; obj = obj->previous)

enum json_type {
    STRING, BOOL, INT, DOUBLE, LIST, JSON_OBJ, JSON_NULL
};

typedef struct _jsonobj jsonobj;

struct _jsonobj {
    char *key;
    enum json_type type;
    jsonobj *parent;
    jsonobj *child;
    jsonobj *previous;
    jsonobj *next;
    union {
        long int_value;
        int bool_value;
        double double_value;
        char *string_value;
        struct {
            size_t size;
            size_t capacity;
            jsonobj **list;
        } list_value;
    } e;
};

jsonobj *jsonobj_new(void);
int jsonobj_parse_str(jsonobj *element, char *str);
void jsonobj_free(jsonobj *head);
char *jsonobj_to_str(jsonobj *head);

void jsonobj_set_str(jsonobj *e, const char *str);
jsonobj *jsonobj_put(jsonobj *parent, const char *key, jsonobj *e);
jsonobj *jsonobj_put_null(jsonobj *previous, const char *key);
jsonobj *jsonobj_put_list(jsonobj *previous, const char *key);
jsonobj *jsonobj_put_jsonobj(jsonobj *previous, const char *key, jsonobj *child);
jsonobj *jsonobj_put_int(jsonobj *previous, const char *key, long n);
jsonobj *jsonobj_put_double(jsonobj *previous, const char *key, double d);
jsonobj *jsonobj_put_bool(jsonobj *previous, const char *key, int b);
jsonobj *jsonobj_put_str(jsonobj *previous, const char *key, const char *str);

void jsonobj_list_add_list(jsonobj *parent, jsonobj *list);
void jsonobj_list_add_jsonobj(jsonobj *list, jsonobj *child);
void jsonobj_list_add_int(jsonobj *list, long n);
void jsonobj_list_add_double(jsonobj *previous, double d);
void jsonobj_list_add_bool(jsonobj *previous, int b);
void jsonobj_list_add_str(jsonobj *previous, const char *str);

jsonobj *jsonobj_lookup(jsonobj *head, const char *key);
jsonobj *jsonobj_list_get_at(jsonobj *list, size_t i);

jsonobj *jsonobj_remove(jsonobj *parent, const char *key);

#endif
