/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Koudai Aono
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objexcept.h"

#if MICROPY_PY_TSTRINGS

extern const mp_obj_type_t mp_type_template;
extern const mp_obj_type_t mp_type_interpolation;

typedef struct _mp_obj_interpolation_t {
    mp_obj_base_t base;
    mp_obj_t value;
    mp_obj_t expression;
    mp_obj_t conversion;    // None or single char string ('r', 's', 'a')
    mp_obj_t format_spec;   // string
} mp_obj_interpolation_t;

typedef struct _mp_obj_template_t {
    mp_obj_base_t base;
    mp_obj_t strings;
    mp_obj_t interpolations;
} mp_obj_template_t;

static void template_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_template_t *self = MP_OBJ_TO_PTR(self_in);
    mp_print_str(print, "Template(strings=");
    mp_obj_print_helper(print, self->strings, PRINT_REPR);
    mp_print_str(print, ", interpolations=");
    mp_obj_print_helper(print, self->interpolations, PRINT_REPR);
    mp_print_str(print, ")");
}

static mp_obj_t template_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    if (n_kw != 0) {
        mp_raise_TypeError(MP_ERROR_TEXT("Template.__new__ only accepts *args arguments"));
    }

    mp_obj_t strings_obj;
    mp_obj_t interpolations_obj;

    if (n_args == 0) {
        mp_obj_t empty_str = mp_obj_new_str("", 0);
        strings_obj = mp_obj_new_tuple(1, &empty_str);
        interpolations_obj = mp_obj_new_tuple(0, NULL);
    } else if (n_args == 2 && mp_obj_is_type(args[0], &mp_type_tuple) && mp_obj_is_type(args[1], &mp_type_tuple)) {
        strings_obj = args[0];
        interpolations_obj = args[1];

        size_t interp_len;
        mp_obj_t *interp_items;
        mp_obj_get_array(interpolations_obj, &interp_len, &interp_items);
        for (size_t i = 0; i < interp_len; i++) {
            if (!mp_obj_is_exact_type(interp_items[i], &mp_type_interpolation)) {
                mp_raise_msg_varg(&mp_type_TypeError,
                    MP_ERROR_TEXT("interpolations tuple must contain only Interpolation objects, got %s"),
                    mp_obj_get_type_str(interp_items[i]));
            }
        }
    } else {
        bool all_strings = true;
        for (size_t i = 0; i < n_args; i++) {
            if (mp_obj_is_exact_type(args[i], &mp_type_interpolation)) {
                all_strings = false;
            } else if (!mp_obj_is_str(args[i])) {
                mp_raise_msg_varg(&mp_type_TypeError,
                    MP_ERROR_TEXT("Template args must be str or Interpolation, got %s"),
                    mp_obj_get_type_str(args[i]));
            }
        }

        if (all_strings) {
            if (n_args == 1) {
                strings_obj = mp_obj_new_tuple(1, &args[0]);
            } else {
                vstr_t vstr;
                vstr_init(&vstr, 16);
                for (size_t i = 0; i < n_args; i++) {
                    size_t str_len;
                    const char *str_data = mp_obj_str_get_data(args[i], &str_len);
                    vstr_add_strn(&vstr, str_data, str_len);
                }
                mp_obj_t concatenated = mp_obj_new_str_from_vstr(&vstr);
                strings_obj = mp_obj_new_tuple(1, &concatenated);
            }
            interpolations_obj = mp_obj_new_tuple(0, NULL);
        } else {
            size_t n_strings = 0;
            size_t n_interpolations = 0;

            for (size_t i = 0; i < n_args; i++) {
                if (mp_obj_is_exact_type(args[i], &mp_type_interpolation)) {
                    n_interpolations++;
                }
            }
            n_strings = n_interpolations + 1;

            mp_obj_t *strings = m_new(mp_obj_t, n_strings);
            mp_obj_t *interpolations = m_new(mp_obj_t, n_interpolations);

            size_t str_idx = 0;
            size_t interp_idx = 0;
            vstr_t current_str;
            vstr_init(&current_str, 16);

            for (size_t i = 0; i < n_args; i++) {
                if (mp_obj_is_exact_type(args[i], &mp_type_interpolation)) {
                    strings[str_idx++] = mp_obj_new_str_from_vstr(&current_str);
                    interpolations[interp_idx++] = args[i];
                    vstr_init(&current_str, 16);
                } else {
                    size_t str_len;
                    const char *str_data = mp_obj_str_get_data(args[i], &str_len);
                    vstr_add_strn(&current_str, str_data, str_len);
                }
            }
            strings[str_idx] = mp_obj_new_str_from_vstr(&current_str);

            strings_obj = mp_obj_new_tuple(n_strings, strings);
            interpolations_obj = mp_obj_new_tuple(n_interpolations, interpolations);

            m_del(mp_obj_t, strings, n_strings);
            m_del(mp_obj_t, interpolations, n_interpolations);
        }
    }

    mp_obj_template_t *self = m_new_obj(mp_obj_template_t);
    self->base.type = type;
    self->strings = strings_obj;
    self->interpolations = interpolations_obj;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t template_str(mp_obj_t self_in) {
    mp_obj_template_t *self = MP_OBJ_TO_PTR(self_in);

    mp_obj_tuple_t *strings = MP_OBJ_TO_PTR(self->strings);
    mp_obj_tuple_t *interps = MP_OBJ_TO_PTR(self->interpolations);

    vstr_t vstr;
    vstr_init(&vstr, 16);
    size_t total_size = 0;

    for (size_t i = 0; i < strings->len; i++) {
        mp_obj_t str_part = strings->items[i];
        size_t str_len;
        const char *str_data = mp_obj_str_get_data(str_part, &str_len);

        total_size += str_len;

        vstr_add_strn(&vstr, str_data, str_len);

        if (i < interps->len) {
            mp_obj_t interp_obj = interps->items[i];

            mp_obj_interpolation_t *interp = MP_OBJ_TO_PTR(interp_obj);
            mp_obj_t value = interp->value;
            mp_obj_t expression = interp->expression;
            mp_obj_t conversion = interp->conversion;
            mp_obj_t format_spec = interp->format_spec;

            size_t expr_len;
            const char *expr_str = mp_obj_str_get_data(expression, &expr_len);
            if (expr_len > 0 && expr_str[expr_len - 1] == '=') {
                vstr_add_strn(&vstr, expr_str, expr_len - 1);
                vstr_add_byte(&vstr, '=');
            }

            const char *conversion_str = NULL;
            const char *format_spec_str = "";
            if (conversion != mp_const_none) {
                conversion_str = mp_obj_str_get_str(conversion);
            }
            format_spec_str = mp_obj_str_get_str(format_spec);

            mp_obj_t evaluated_format_spec = format_spec;
            if (*format_spec_str) {
                const char *p = format_spec_str;
                bool has_interpolation = false;
                while (*p) {
                    if (*p == '{') {
                        if (*(p + 1) && *(p + 1) != '{') {
                            has_interpolation = true;
                            break;
                        } else if (*(p + 1) && *(p + 1) == '{') {
                            p += 2;
                            continue;
                        }
                    } else if (*p == '}') {
                        if (*(p + 1) && *(p + 1) == '}') {
                            p += 2;
                            continue;
                        } else {
                            has_interpolation = true;
                            break;
                        }
                    }
                    p++;
                }

                if (has_interpolation) {
                    vstr_t eval_vstr;
                    vstr_init(&eval_vstr, 16);
                    p = format_spec_str;
                    while (*p) {
                        if (*p == '{' && *(p + 1) && *(p + 1) != '{') {
                            p++;
                            const char *var_start = p;
                            while (*p && *p != '}' && *p != ':' && *p != '!') {
                                p++;
                            }
                            if (*p == '}') {
                                size_t var_len = p - var_start;
                                mp_obj_t var_value = mp_load_name(qstr_from_strn(var_start, var_len));

                                vstr_t var_str;
                                vstr_init(&var_str, 16);
                                mp_print_t var_print;
                                vstr_init_print(&var_str, 16, &var_print);
                                mp_obj_print_helper(&var_print, var_value, PRINT_STR);
                                vstr_add_strn(&eval_vstr, var_str.buf, var_str.len);
                                vstr_clear(&var_str);

                                p++;
                            }
                        } else if (*p == '{' && *(p + 1) && *(p + 1) == '{') {
                            vstr_add_byte(&eval_vstr, '{');
                            p += 2;
                        } else if (*p == '}' && *(p + 1) && *(p + 1) == '}') {
                            vstr_add_byte(&eval_vstr, '}');
                            p += 2;
                        } else {
                            vstr_add_byte(&eval_vstr, *p);
                            p++;
                        }
                    }
                    evaluated_format_spec = mp_obj_new_str_from_vstr(&eval_vstr);
                    format_spec_str = mp_obj_str_get_str(evaluated_format_spec);
                } else {
                    bool has_escaped_braces = false;
                    p = format_spec_str;
                    while (*p) {
                        if ((*p == '{' && *(p + 1) && *(p + 1) == '{') ||
                            (*p == '}' && *(p + 1) && *(p + 1) == '}')) {
                            has_escaped_braces = true;
                            break;
                        }
                        p++;
                    }

                    if (has_escaped_braces) {
                        vstr_t unesc_vstr;
                        vstr_init(&unesc_vstr, 16);
                        p = format_spec_str;
                        while (*p) {
                            if (*p == '{' && *(p + 1) && *(p + 1) == '{') {
                                vstr_add_byte(&unesc_vstr, '{');
                                p += 2;
                            } else if (*p == '}' && *(p + 1) && *(p + 1) == '}') {
                                vstr_add_byte(&unesc_vstr, '}');
                                p += 2;
                            } else {
                                vstr_add_byte(&unesc_vstr, *p);
                                p++;
                            }
                        }
                        evaluated_format_spec = mp_obj_new_str_from_vstr(&unesc_vstr);
                        format_spec_str = mp_obj_str_get_str(evaluated_format_spec);
                    }
                }
            }

            mp_obj_t conv_value = value;
            if (conversion_str && *conversion_str) {
                if (*conversion_str == 'r') {
                    vstr_t repr_vstr;
                    vstr_init(&repr_vstr, 16);
                    mp_print_t repr_print;
                    vstr_init_print(&repr_vstr, 16, &repr_print);
                    mp_obj_print_helper(&repr_print, value, PRINT_REPR);
                    conv_value = mp_obj_new_str_from_vstr(&repr_vstr);
                } else if (*conversion_str == 's') {
                    vstr_t str_vstr;
                    vstr_init(&str_vstr, 16);
                    mp_print_t str_print;
                    vstr_init_print(&str_vstr, 16, &str_print);
                    mp_obj_print_helper(&str_print, value, PRINT_STR);
                    conv_value = mp_obj_new_str_from_vstr(&str_vstr);
                } else if (*conversion_str == 'a') {
                    vstr_t repr_vstr;
                    vstr_init(&repr_vstr, 16);
                    mp_print_t repr_print;
                    vstr_init_print(&repr_vstr, 16, &repr_print);
                    mp_obj_print_helper(&repr_print, value, PRINT_REPR);
                    conv_value = mp_obj_new_str_from_vstr(&repr_vstr);
                }
            }

            if (*format_spec_str) {
                const char *check = format_spec_str;
                while (*check) {
                    if (*check == '{' || *check == '}') {
                        mp_raise_ValueError(MP_ERROR_TEXT("invalid format spec - cannot contain braces"));
                    }
                    check++;
                }

                vstr_t fmt_vstr;
                vstr_init(&fmt_vstr, 16);
                vstr_add_str(&fmt_vstr, "{:");
                vstr_add_str(&fmt_vstr, format_spec_str);
                vstr_add_str(&fmt_vstr, "}");
                mp_obj_t fmt_str = mp_obj_new_str_from_vstr(&fmt_vstr);
                mp_obj_t fmt_args[2] = {fmt_str, conv_value};
                conv_value = mp_obj_str_format(2, fmt_args, NULL);
            } else {
                if (conversion_str == NULL) {
                    vstr_t str_vstr;
                    vstr_init(&str_vstr, 16);
                    mp_print_t str_print;
                    vstr_init_print(&str_vstr, 16, &str_print);
                    mp_obj_print_helper(&str_print, value, PRINT_STR);
                    conv_value = mp_obj_new_str_from_vstr(&str_vstr);
                }
            }

            size_t val_len;
            const char *val_str = mp_obj_str_get_data(conv_value, &val_len);
            vstr_add_strn(&vstr, val_str, val_len);
        }
    }

    return mp_obj_new_str_from_vstr(&vstr);
}

MP_DEFINE_CONST_FUN_OBJ_1(template_str_obj, template_str);

typedef struct _mp_obj_template_iter_t {
    mp_obj_base_t base;
    mp_obj_t template;
    size_t index;
    bool at_interpolation;
} mp_obj_template_iter_t;

static mp_obj_t template_iter_iternext(mp_obj_t self_in) {
    mp_obj_template_iter_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_template_t *tmpl = MP_OBJ_TO_PTR(self->template);
    mp_obj_tuple_t *strings = MP_OBJ_TO_PTR(tmpl->strings);
    mp_obj_tuple_t *interps = MP_OBJ_TO_PTR(tmpl->interpolations);

    while (self->index < strings->len || (self->index < interps->len && self->at_interpolation)) {
        if (!self->at_interpolation) {
            if (self->index < strings->len) {
                mp_obj_t str_obj = strings->items[self->index];
                size_t str_len;
                mp_obj_str_get_data(str_obj, &str_len);

                if (str_len > 0) {
                    self->at_interpolation = true;
                    return str_obj;
                } else {
                    self->at_interpolation = true;
                }
            }
        }

        if (self->at_interpolation) {
            if (self->index < interps->len) {
                mp_obj_t interp = interps->items[self->index];
                self->index++;
                self->at_interpolation = false;
                return interp;
            } else {
                self->index++;
                self->at_interpolation = false;
            }
        }
    }

    return MP_OBJ_STOP_ITERATION;
}

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_template_iter,
    MP_QSTR_template_iterator,
    MP_TYPE_FLAG_ITER_IS_ITERNEXT,
    iter, template_iter_iternext
    );

static mp_obj_t template_iter(mp_obj_t self_in, mp_obj_iter_buf_t *iter_buf) {
    assert(sizeof(mp_obj_template_iter_t) <= sizeof(mp_obj_iter_buf_t));
    mp_obj_template_iter_t *iter = (mp_obj_template_iter_t *)iter_buf;
    iter->base.type = &mp_type_template_iter;
    iter->template = self_in;
    iter->index = 0;
    iter->at_interpolation = false;
    return MP_OBJ_FROM_PTR(iter);
}

static void template_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_template_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR_strings) {
            dest[0] = self->strings;
        } else if (attr == MP_QSTR_interpolations) {
            dest[0] = self->interpolations;
        } else if (attr == MP_QSTR_values) {
            mp_obj_tuple_t *interps = MP_OBJ_TO_PTR(self->interpolations);

            if (interps->len <= 4) {
                mp_obj_t values[4];
                for (size_t i = 0; i < interps->len; i++) {
                    mp_obj_interpolation_t *interp = MP_OBJ_TO_PTR(interps->items[i]);
                    values[i] = interp->value;
                }
                dest[0] = mp_obj_new_tuple(interps->len, values);
            } else {
                mp_obj_t *values = m_new(mp_obj_t, interps->len);
                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    for (size_t i = 0; i < interps->len; i++) {
                        mp_obj_interpolation_t *interp = MP_OBJ_TO_PTR(interps->items[i]);
                        values[i] = interp->value;
                    }
                    mp_obj_t result = mp_obj_new_tuple(interps->len, values);
                    nlr_pop();
                    m_del(mp_obj_t, values, interps->len);
                    dest[0] = result;
                } else {
                    m_del(mp_obj_t, values, interps->len);
                    nlr_jump(nlr.ret_val);
                }
            }
        } else if (attr == MP_QSTR___str__) {
            dest[0] = mp_obj_new_bound_meth(MP_OBJ_FROM_PTR(&template_str_obj), self_in);
        }
    }
}

static mp_obj_t template_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    mp_obj_template_t *lhs = MP_OBJ_TO_PTR(lhs_in);

    switch (op) {
        case MP_BINARY_OP_ADD: {
            if (!mp_obj_is_exact_type(rhs_in, &mp_type_template)) {
                if (mp_obj_is_str(rhs_in)) {
                    mp_raise_TypeError(MP_ERROR_TEXT("can only concatenate Template (not \"str\") to Template"));
                }
                return MP_OBJ_NULL;
            }

            mp_obj_template_t *rhs = MP_OBJ_TO_PTR(rhs_in);

            mp_obj_tuple_t *lhs_strings = MP_OBJ_TO_PTR(lhs->strings);
            mp_obj_tuple_t *lhs_interps = MP_OBJ_TO_PTR(lhs->interpolations);
            mp_obj_tuple_t *rhs_strings = MP_OBJ_TO_PTR(rhs->strings);
            mp_obj_tuple_t *rhs_interps = MP_OBJ_TO_PTR(rhs->interpolations);

            size_t new_strings_len = lhs_strings->len + rhs_strings->len - 1;
            size_t new_interps_len = lhs_interps->len + rhs_interps->len;

            mp_obj_t *new_strings_items = m_new(mp_obj_t, new_strings_len);

            for (size_t i = 0; i < lhs_strings->len - 1; i++) {
                new_strings_items[i] = lhs_strings->items[i];
            }

            size_t lhs_last_len, rhs_first_len;
            const char *lhs_last_str = mp_obj_str_get_data(lhs_strings->items[lhs_strings->len - 1], &lhs_last_len);
            const char *rhs_first_str = mp_obj_str_get_data(rhs_strings->items[0], &rhs_first_len);

            vstr_t vstr;
            vstr_init(&vstr, lhs_last_len + rhs_first_len);
            vstr_add_strn(&vstr, lhs_last_str, lhs_last_len);
            vstr_add_strn(&vstr, rhs_first_str, rhs_first_len);
            new_strings_items[lhs_strings->len - 1] = mp_obj_new_str_from_vstr(&vstr);

            for (size_t i = 1; i < rhs_strings->len; i++) {
                new_strings_items[lhs_strings->len - 1 + i] = rhs_strings->items[i];
            }

            mp_obj_t *new_interps_items = m_new(mp_obj_t, new_interps_len);

            for (size_t i = 0; i < lhs_interps->len; i++) {
                new_interps_items[i] = lhs_interps->items[i];
            }

            for (size_t i = 0; i < rhs_interps->len; i++) {
                new_interps_items[lhs_interps->len + i] = rhs_interps->items[i];
            }

            mp_obj_t new_strings_tuple = mp_obj_new_tuple(new_strings_len, new_strings_items);
            mp_obj_t new_interps_tuple = mp_obj_new_tuple(new_interps_len, new_interps_items);

            m_del(mp_obj_t, new_strings_items, new_strings_len);
            m_del(mp_obj_t, new_interps_items, new_interps_len);

            mp_obj_template_t *result = mp_obj_malloc(mp_obj_template_t, &mp_type_template);
            result->strings = new_strings_tuple;
            result->interpolations = new_interps_tuple;
            return MP_OBJ_FROM_PTR(result);
        }

        default:
            return MP_OBJ_NULL; // op not supported
    }
}

static const mp_rom_map_elem_t template_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___str__), MP_ROM_PTR(&template_str_obj) },
};
static MP_DEFINE_CONST_DICT(template_locals_dict, template_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_template,
    MP_QSTR_Template,
    MP_TYPE_FLAG_NONE,
    make_new, template_make_new,
    print, template_print,
    attr, template_attr,
    iter, template_iter,
    binary_op, template_binary_op,
    locals_dict, &template_locals_dict
    );

static mp_obj_t mp_builtin___template__(mp_obj_t strings, mp_obj_t interpolations_in) {
    mp_obj_t *items;
    size_t len;
    mp_obj_get_array(interpolations_in, &len, &items);

    mp_obj_t *interpolations = m_new(mp_obj_t, len);
    for (size_t i = 0; i < len; i++) {
        mp_obj_t *interp_items;
        size_t interp_len;
        mp_obj_get_array(items[i], &interp_len, &interp_items);

        if (interp_len >= 2) {
            mp_obj_t value = interp_items[0];
            mp_obj_t expression = interp_items[1];
            mp_obj_t conversion = (interp_len > 2) ? interp_items[2] : mp_const_none;
            mp_obj_t format_spec = (interp_len > 3) ? interp_items[3] : MP_OBJ_NEW_QSTR(MP_QSTR_);

            interpolations[i] = mp_obj_new_interpolation(value, expression, conversion, format_spec);
        } else {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid interpolation format"));
        }
    }

    mp_obj_t interpolations_tuple = mp_obj_new_tuple(len, interpolations);
    m_del(mp_obj_t, interpolations, len);

    // Create Template directly instead of calling make_new
    mp_obj_template_t *self = mp_obj_malloc(mp_obj_template_t, &mp_type_template);
    self->strings = strings;
    self->interpolations = interpolations_tuple;
    return MP_OBJ_FROM_PTR(self);
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_builtin___template___obj, mp_builtin___template__);

#endif
