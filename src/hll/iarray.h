/* 版权所有 (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * 本程序是自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （第 2 版，或你选择的任何更高版本）的条款，重新分发和/或修改本程序。
 *
 * 分发本程序是希望它有用，但不作任何担保，甚至不担保适销性或
 * 适用于特定用途。详见 GNU 通用公共许可证。
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，见 <http://gnu.org/licenses/>。
 */

#ifndef SYSTEM4_HLL_IARRAY_H
#define SYSTEM4_HLL_IARRAY_H

#include <stddef.h>
#include <stdbool.h>
#include "gfx/types.h"
#include "gfx/font.h"

struct string;
struct page;
struct ain_type;

// TODO: implement using array page directly?
struct iarray_writer {
	unsigned allocated;
	unsigned size;
	int *data;
};

void iarray_init_writer(struct iarray_writer *w, const char *header);
void iarray_free_writer(struct iarray_writer *w);
void iarray_write(struct iarray_writer *w, int data);
void iarray_write_at(struct iarray_writer *w, unsigned pos, int data);
void iarray_write_float(struct iarray_writer *w, float data);
void iarray_write_string(struct iarray_writer *w, struct string *s);
void iarray_write_string_or_null(struct iarray_writer *w, struct string *s);
void iarray_write_struct(struct iarray_writer *w, struct page *page, bool with_type);
void iarray_write_array(struct iarray_writer *w, struct page *page);
void iarray_write_point(struct iarray_writer *w, Point *p);
void iarray_write_rectangle(struct iarray_writer *w, Rectangle *r);
void iarray_write_color(struct iarray_writer *w, SDL_Color *color);
void iarray_write_text_style(struct iarray_writer *w, struct text_style *style);

static inline unsigned iarray_writer_pos(struct iarray_writer *w)
{
	return w->size;
}

struct page *iarray_to_page(struct iarray_writer *w);
uint8_t *iarray_to_buffer(struct iarray_writer *w, size_t *size_out);

struct iarray_reader {
	union vm_value *data;
	unsigned size;
	unsigned pos;
	int error;
};

static inline bool iarray_end(struct iarray_reader *r)
{
	return r->pos >= r->size;
}

bool iarray_init_reader(struct iarray_reader *r, struct page *a, const char *header);
int iarray_read(struct iarray_reader *r);
float iarray_read_float(struct iarray_reader *r);
struct string *iarray_read_string(struct iarray_reader *r);
struct string *iarray_read_string_or_null(struct iarray_reader *r);
struct page *iarray_read_struct(struct iarray_reader *r, int struct_type, bool with_type);
struct page *iarray_read_array(struct iarray_reader *r, struct ain_type *t);
void iarray_read_point(struct iarray_reader *r, Point *p);
void iarray_read_rectangle(struct iarray_reader *r, Rectangle *rect);
void iarray_read_color(struct iarray_reader *r, SDL_Color *color);
void iarray_read_text_style(struct iarray_reader *r, struct text_style *style);

#endif /* SYSTEM4_HLL_IARRAY_H */
