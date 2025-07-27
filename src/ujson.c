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

#include <stdlib.h>
#include <string.h>

#include "ujson.h"

static char *str_clone(const char *str)
{
	if (!str)
		return NULL;
	long str_sz = strlen(str);
	char *clone = (char*) malloc((str_sz + 1) * sizeof(char));
	if (clone) 
		strcpy(clone, str);
		
	return clone;
	
}

static int skip_spaces(char **buf)
{
    if ((**buf) == '\0')
        return -1;

    while ((**buf) == '\n' || (**buf) == ' ' || (**buf) == '\r' || (**buf) == '\t')
        (*buf)++;
    return 0;
}

static inline int is_digit(char c) {
    return c == '+' || c == '-' || c == '.' || (c >= 0x30 && c <= 0x39) || c == 'e';
}

static char *parse_quoted_str(char **buf)
{
    if ((**buf) == '"') {
        (*buf)++;
        
        char *end_ptr = strchr((*buf), '"');
        if (!end_ptr) {
            return NULL;
        }

        char *str = (char*) malloc((1 + end_ptr - *buf) * sizeof(*str));
        int i = 0;
        while((*buf) < end_ptr) {
            str[i] = (**buf);
            (*buf)++;
            i++;
        }
        str[i] = '\0';
        (*buf)++;
        return str;
    }
    return NULL;
}

jsonobj *jsonobj_new(void)
{
	jsonobj *e = (jsonobj*) malloc(sizeof(*e));
	e->size = 0;
	e->next = NULL;
	e->key = NULL;
	e->previous = NULL;
	e->child = NULL;
	e->parent = NULL;
	e->type = JSON_NULL;
    e->e.bool_value = 0;
    e->e.int_value = 0;
    e->e.double_value = 0.0;
    e->e.string_value = NULL;
	return e;
}

void jsonobj_free(jsonobj *head)
{
    if (!head) return;

    jsonobj *e = NULL, *o = NULL;
    switch (head->type) {
    case STRING:
        free(head->e.string_value);
        break;
    case LIST:
    case JSON_OBJ:
        if (head->child) {
            e = head->child->previous;
            while (e) {
                o = e->previous;
                jsonobj_free(e);
                e = o;
            }
        }

        jsonobj_free(head->child);
        break;

    case JSON_NULL:
    case BOOL:
    case INT:
    case DOUBLE:
        break;
    }
    if (head->key)
        free(head->key);
    free(head);
}

static void put(jsonobj *parent, jsonobj *e)
{
	jsonobj *prev = NULL;
	if (!parent || !e)
		return;
		
	if (parent->child) {
		prev = parent->child;
		prev->next = e;
	}
	e->parent = parent;
	e->previous = prev;
	parent->child = e;
	parent->size++;
}

static int parse_buff(jsonobj *element, char **buf)
{
    char *key = NULL;
    jsonobj *e = NULL;
    if (skip_spaces(buf)) {
        return -1;
    }

	switch ((**buf)) {
	case '{': {	    
		element->size = 0;
		element->type = JSON_OBJ;
		(*buf)++;
        if (skip_spaces(buf))
            goto parsing_fail;

		while ((**buf) != '}') {
			skip_spaces(buf);
            key = parse_quoted_str(buf);
            if (!key)
                goto parsing_fail;

            if (skip_spaces(buf))
                goto parsing_fail;

            if ((**buf) != ':')
                goto parsing_fail;

			(*buf)++;
			
			e = jsonobj_new();
            if (parse_buff(e, buf))
                goto parsing_fail;
			
            if (skip_spaces(buf))
                goto parsing_fail;

			if ((**buf) == ',') 
				(*buf)++;
			else if ((**buf) != '}')
                goto parsing_fail;

			e->key = key;
			put(element, e);
		}

		(*buf)++;
		return 0;
	    
	}
	case '[': {		
		element->size = 0;
		element->type = LIST;
		(*buf)++;
        if (skip_spaces(buf))
            goto parsing_fail;

		while ((**buf) != ']') {
			e = jsonobj_new();
            if (parse_buff(e, buf))
                goto parsing_fail;

            if (skip_spaces(buf))
                goto parsing_fail;
			if ((**buf) == ',') 
				(*buf)++;
			else if ((**buf) != ']')
                goto parsing_fail;
			    
			put(element, e);
		}

		(*buf)++;
		return 0;
	    
	}
	case '"': {
		element->type = STRING;
        element->e.string_value = parse_quoted_str(buf);
        return !element->e.string_value;
	}
	case 't': {
		if (!strncmp(*buf, "true", 4)) {
			element->type = BOOL;
            element->e.bool_value = 1;
			(*buf) += 4;
			return 0;
		}

		return 1;
	}
	case 'f': {
		if (!strncmp(*buf, "false", 5)) {
			element->type = BOOL;
            element->e.bool_value = 0;
			(*buf) += 5;
			return 0;
		}

		return 1;
	}
	case 'n': {
		if (!strncmp(*buf, "null", 4)) {
			element->type = JSON_NULL;
			(*buf) += 4;
			return 0;
		}

		return 1;
	}
	default: {
		char numbuf[256];
		char *start_ptr = (*buf);
        for (; is_digit(**buf); (*buf)++);
        strncpy(numbuf, start_ptr, (*buf) - start_ptr + 1);

        // add exponential notation
        if (strchr(numbuf, '.')) {
            element->e.double_value = atof(numbuf);
            element->type = DOUBLE;
        } else {
            element->e.int_value = atol(numbuf);
            element->type = INT;
        }

		return 0;
	}
    }
parsing_fail:
    if (key)
        free(key);
    jsonobj_free(e);
    return -1;
}

int jsonobj_parse_str(jsonobj *element, char *str)
{
	if (!str)
		return -5;
	char *strbufp = str;
    return parse_buff(element, &strbufp);
}

char *jsonobj_to_str(jsonobj *head)
{	
	jsonobj *e = NULL;
	enum json_type t = head->type;
	int str_capacity = 1;
	switch (t) {
	case STRING:
        str_capacity += strlen(head->e.string_value) + 2;
		break;
	case INT:
	case DOUBLE:
		str_capacity += 20;
		break;
	case LIST:
	case JSON_OBJ:
        str_capacity += 2;
		break;
	case BOOL:
	case JSON_NULL:
		str_capacity += 5;
		break;
	}
    char *str = (char*) malloc(str_capacity * sizeof(char));
    memset(str, '\0', str_capacity);
	switch (t) {
	case STRING:
        sprintf(str, "\"%s\"", head->e.string_value);
		break;
	case BOOL:
        sprintf(str, "%s", head->e.bool_value ? "true" : "false");
		break;
	case INT:
        sprintf(str, "%ld", head->e.int_value);
		break;
	case DOUBLE:
        sprintf(str, "%f", head->e.double_value);
		break;
	case LIST:
		sprintf(str, "[");
		for (e = head->child; e && e->previous; e = e->previous);
		for (; e; e = e->next) {
			char *istr = jsonobj_to_str(e);
            str_capacity += strlen(istr) + 3;
			str = (char*) realloc(str, str_capacity * sizeof(*str));
			strcat(str, istr);
			if (e->next)
				strcat(str, ",");
			free(istr);
		}
		strcat(str, "]");
		break;
	case JSON_OBJ:
		sprintf(str, "{");

        for (e = head->child; e && e->previous; e = e->previous);
        for (; e; e = e->next) {

			char *istr = jsonobj_to_str(e);
            str_capacity += strlen(e->key) + strlen(istr) + 4;
            str = (char*) realloc(str, str_capacity * sizeof(char));
            strcat(str, "\"");
            strcat(str, e->key);
            strcat(str, "\":");
            strcat(str, istr);
            if (e->next)
				strcat(str, ",");
			free(istr);
		}
		strcat(str, "}");
		break;
	case JSON_NULL:
		sprintf(str, "null");
		break;
	}
	return str;
}

void jsonobj_set_str(jsonobj *e, const char *str)
{
    e->type = STRING;
    e->e.string_value = str_clone(str);
}

jsonobj *jsonobj_put(jsonobj *parent, const char *key, jsonobj *e)
{
    e->key = str_clone(key);
    put(parent, e);
    return e;
}

jsonobj *jsonobj_put_null(jsonobj *parent, const char *key)
{
	jsonobj *e = jsonobj_new();
	e->type = JSON_NULL;
	e->key = str_clone(key);
	put(parent, e);
	return e;
}

jsonobj *jsonobj_put_jsonobj(jsonobj *parent, const char *key, jsonobj *child)
{
    jsonobj *e = jsonobj_new();
    e->type = JSON_OBJ;
    e->key = str_clone(key);
    put(e, child);
    put(parent, e);
    return e;
}

jsonobj *jsonobj_put_list(jsonobj *parent, const char *key)
{
    jsonobj *e = jsonobj_new();
    e->type = LIST;
    e->key = str_clone(key);
    put(parent, e);
    return e;
}

jsonobj *jsonobj_put_int(jsonobj *parent, const char *key, long n)
{
	jsonobj *e = jsonobj_new();
	e->type = INT;
	e->key = str_clone(key);
    e->e.int_value = n;
	put(parent, e);
	return e;
}
jsonobj *jsonobj_put_double(jsonobj *parent, const char *key, double d)
{
	jsonobj *e = jsonobj_new();
	e->type = DOUBLE;
	e->key = str_clone(key);
    e->e.double_value = d;
	put(parent, e);
	return e;
}

jsonobj *jsonobj_put_bool(jsonobj *parent, const char *key, int b)
{
	jsonobj *e = jsonobj_new();
	e->type = BOOL;
	e->key = str_clone(key);
    e->e.bool_value = b;
	put(parent, e);
	return e;
}

jsonobj *jsonobj_put_str(jsonobj *parent, const char *key, const char *str)
{
	jsonobj *e = jsonobj_new();
    e->type = STRING;
    e->e.string_value = str_clone(str);
	e->key = str_clone(key);
	put(parent, e);
	return e;
}

void jsonobj_list_add_null(jsonobj *list)
{
	jsonobj_put_null(list, NULL);
}

void jsonobj_list_add_list(jsonobj *list, jsonobj *child)
{
    child->type = LIST;
    put(list, child);
}

void jsonobj_list_add_jsonobj(jsonobj *list, jsonobj *child)
{
    child->type = JSON_OBJ;
    put(list, child);
}

void jsonobj_list_add_int(jsonobj *list, long n)
{
	jsonobj_put_int(list, NULL, n);
}

void jsonobj_list_add_double(jsonobj *list, double d)
{
	jsonobj_put_double(list, NULL, d);
}

void jsonobj_list_add_bool(jsonobj *list, int b)
{
	jsonobj_put_bool(list, NULL, b);
}

void jsonobj_list_add_str(jsonobj *list, const char *str)
{
	jsonobj_put_str(list, NULL, str);
}

jsonobj *jsonobj_lookup(jsonobj *head, const char *key)
{
	jsonobj *e = NULL;
	for (e = head->child; e; e = e->previous) {
		if (!strcmp(e->key, key))
			return e;
	}
	return e;
}

jsonobj *jsonobj_list_get_at(jsonobj *list, int i)
{
	jsonobj *e = NULL;
	int lindex = list->size - 1;
    for (e = list->child; e; e = e->previous, lindex--) {
        if (lindex == i) {
            return e;
        }
    }
	
    return NULL;
}

jsonobj *jsonobj_remove(jsonobj *parent, const char *key)
{
	jsonobj *e = jsonobj_lookup(parent, key);
	if (e) {
		jsonobj *prev = e->previous;
		jsonobj *next = e->next;
		if (prev)
			prev->next = e->next;
		
		if (next)
			next->previous = prev;

		if (parent->child == e)
			parent->child = prev;

		e->next = NULL;
		e->previous = NULL;
		e->parent = NULL;	
		parent->size--;
	}
	return e;
}


