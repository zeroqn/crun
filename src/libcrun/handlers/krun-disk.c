/*
 * crun - OCI runtime written in C
 *
 * Copyright (C) 2026 Giuseppe Scrivano <giuseppe@scrivano.org>
 * crun is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * crun is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with crun.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE

#include <config.h>
#include "krun-disk.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNLIKELY
#  define UNLIKELY(x) __builtin_expect ((x), 0)
#endif

#define KRUN_DISK_ANNOTATION_PREFIX "krun.disk."

struct krun_disk_annotation_field_s
{
  size_t index;
  int field;
  const char *value;
  const char *name;
};

enum
{
  KRUN_DISK_FIELD_PATH = 0,
  KRUN_DISK_FIELD_ID,
  KRUN_DISK_FIELD_READONLY,
};

struct krun_disk_builder_s
{
  char *path;
  char *id;
  bool readonly;
  bool has_path;
  bool has_id;
  bool has_readonly;
};

static int
parse_readonly_annotation (const char *value, bool *readonly, libcrun_error_t *err)
{
  if (value == NULL)
    return crun_make_error (err, 0, "invalid krun disk readonly value: expected `true` or `false`");

  if (strcmp (value, "true") == 0)
    {
      *readonly = true;
      return 0;
    }
  if (strcmp (value, "false") == 0)
    {
      *readonly = false;
      return 0;
    }

  return crun_make_error (err, 0, "invalid krun disk readonly value `%s`: expected `true` or `false`", value);
}

static int
parse_annotation_index (const char *value, const char **end, size_t *index, libcrun_error_t *err)
{
  const unsigned char *p = (const unsigned char *) value;
  size_t n = 0;

  if (*p == '\0')
    return crun_make_error (err, 0, "invalid krun disk annotation index: empty index");

  if (*p == '0')
    {
      p++;
      if (isdigit (*p))
        return crun_make_error (err, 0, "invalid krun disk annotation index `%s`: leading zeros are not allowed", value);
      *index = 0;
      *end = (const char *) p;
      return 0;
    }

  if (! isdigit (*p) || *p == '+' || *p == '-')
    return crun_make_error (err, 0, "invalid krun disk annotation index `%s`: expected canonical non-negative decimal", value);

  for (; isdigit (*p); p++)
    {
      size_t digit = (size_t) (*p - '0');
      if (n > (SIZE_MAX - digit) / 10)
        return crun_make_error (err, 0, "invalid krun disk annotation index `%s`: value is too large", value);
      n = n * 10 + digit;
    }

  *index = n;
  *end = (const char *) p;
  return 0;
}

static int
parse_annotation_key (const char *key, size_t *index, int *field, libcrun_error_t *err)
{
  const char *suffix;
  const char *field_name;
  int ret;

  suffix = key + strlen (KRUN_DISK_ANNOTATION_PREFIX);
  ret = parse_annotation_index (suffix, &field_name, index, err);
  if (UNLIKELY (ret < 0))
    return ret;

  if (*field_name != '.' || field_name[1] == '\0')
    return crun_make_error (err, 0, "invalid krun disk annotation `%s`: expected krun.disk.N.path, krun.disk.N.id, or krun.disk.N.readonly", key);

  field_name++;
  if (strcmp (field_name, "path") == 0)
    *field = KRUN_DISK_FIELD_PATH;
  else if (strcmp (field_name, "id") == 0)
    *field = KRUN_DISK_FIELD_ID;
  else if (strcmp (field_name, "readonly") == 0)
    *field = KRUN_DISK_FIELD_READONLY;
  else
    return crun_make_error (err, 0, "unsupported krun disk annotation `%s`", key);

  return 0;
}

static int
compare_annotation_fields (const void *a, const void *b)
{
  const struct krun_disk_annotation_field_s *fa = a;
  const struct krun_disk_annotation_field_s *fb = b;

  if (fa->index < fb->index)
    return -1;
  if (fa->index > fb->index)
    return 1;
  return fa->field - fb->field;
}

static char *
dup_string (const char *value, libcrun_error_t *err)
{
  char *ret = strdup (value);
  if (ret == NULL)
    crun_make_error (err, errno, "copy krun disk configuration string");
  return ret;
}

static int
grow_array (void **ptr, size_t element_size, size_t n_elements, libcrun_error_t *err)
{
  void *tmp;

  if (n_elements > SIZE_MAX / element_size)
    return crun_make_error (err, EOVERFLOW, "grow krun disk configuration array");

  tmp = realloc (*ptr, element_size * n_elements);
  if (tmp == NULL)
    return crun_make_error (err, errno, "grow krun disk configuration array");

  *ptr = tmp;
  return 0;
}

static int
append_annotation_field (struct krun_disk_annotation_field_s **fields, size_t *n_fields,
                         size_t index, int field, const char *value, const char *name, libcrun_error_t *err)
{
  int ret = grow_array ((void **) fields, sizeof (**fields), *n_fields + 1, err);
  if (UNLIKELY (ret < 0))
    return ret;

  (*fields)[*n_fields].index = index;
  (*fields)[*n_fields].field = field;
  (*fields)[*n_fields].value = value;
  (*fields)[*n_fields].name = name;
  (*n_fields)++;
  return 0;
}

static void
free_builders (struct krun_disk_builder_s *builders, size_t n_builders)
{
  size_t i;

  if (builders == NULL)
    return;

  for (i = 0; i < n_builders; i++)
    {
      free (builders[i].path);
      free (builders[i].id);
    }
  free (builders);
}

static int
parse_disk_annotations (string_map *annotations, struct krun_disk_config_s **disks,
                        size_t *n_disks, bool *found, libcrun_error_t *err)
{
  struct krun_disk_annotation_field_s *fields = NULL;
  struct krun_disk_builder_s *builders = NULL;
  size_t n_fields = 0;
  size_t annotations_len;
  size_t i;
  size_t expected_index = 0;
  size_t n_builders = 0;
  int ret;

  *found = false;
  annotations_len = annotations ? string_map_size (annotations) : 0;

  for (i = 0; i < annotations_len; i++)
    {
      const char *name = NULL;
      const char *value = NULL;
      size_t index;
      int field;

      ret = string_map_get_at (annotations, i, &name, &value);
      if (UNLIKELY (ret < 0))
        {
          free (fields);
          return crun_make_error (err, errno, "read krun disk annotation");
        }

      if (strncmp (name, KRUN_DISK_ANNOTATION_PREFIX, strlen (KRUN_DISK_ANNOTATION_PREFIX)) != 0)
        continue;

      *found = true;
      ret = parse_annotation_key (name, &index, &field, err);
      if (UNLIKELY (ret < 0))
        {
          free (fields);
          return ret;
        }

      ret = append_annotation_field (&fields, &n_fields, index, field, value, name, err);
      if (UNLIKELY (ret < 0))
        {
          free (fields);
          return ret;
        }
    }

  if (! *found)
    return 0;

  qsort (fields, n_fields, sizeof (*fields), compare_annotation_fields);

  for (i = 0; i < n_fields;)
    {
      size_t index = fields[i].index;
      struct krun_disk_builder_s *builder;

      if (index != expected_index)
        {
          free (fields);
          free_builders (builders, n_builders);
          return crun_make_error (err, 0, "krun disk annotation indexes must be contiguous from 0: missing index %zu", expected_index);
        }

      ret = grow_array ((void **) &builders, sizeof (*builders), n_builders + 1, err);
      if (UNLIKELY (ret < 0))
        {
          free (fields);
          free_builders (builders, n_builders);
          return ret;
        }
      memset (&builders[n_builders], 0, sizeof (builders[n_builders]));
      builder = &builders[n_builders];
      n_builders++;

      for (; i < n_fields && fields[i].index == index; i++)
        {
          switch (fields[i].field)
            {
            case KRUN_DISK_FIELD_PATH:
              if (builder->has_path)
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return crun_make_error (err, 0, "duplicate krun disk path annotation for index %zu", index);
                }
              if (fields[i].value == NULL || fields[i].value[0] == '\0')
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return crun_make_error (err, 0, "krun disk annotation krun.disk.%zu.path must not be empty", index);
                }
              builder->path = dup_string (fields[i].value, err);
              if (UNLIKELY (builder->path == NULL))
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return -1;
                }
              builder->has_path = true;
              break;

            case KRUN_DISK_FIELD_ID:
              if (builder->has_id)
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return crun_make_error (err, 0, "duplicate krun disk id annotation for index %zu", index);
                }
              if (fields[i].value == NULL || fields[i].value[0] == '\0')
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return crun_make_error (err, 0, "krun disk annotation krun.disk.%zu.id must not be empty", index);
                }
              builder->id = dup_string (fields[i].value, err);
              if (UNLIKELY (builder->id == NULL))
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return -1;
                }
              builder->has_id = true;
              break;

            case KRUN_DISK_FIELD_READONLY:
              if (builder->has_readonly)
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return crun_make_error (err, 0, "duplicate krun disk readonly annotation for index %zu", index);
                }
              ret = parse_readonly_annotation (fields[i].value, &builder->readonly, err);
              if (UNLIKELY (ret < 0))
                {
                  free (fields);
                  free_builders (builders, n_builders);
                  return ret;
                }
              builder->has_readonly = true;
              break;
            }
        }

      if (! builder->has_path)
        {
          free (fields);
          free_builders (builders, n_builders);
          return crun_make_error (err, 0, "missing required krun disk annotation krun.disk.%zu.path", index);
        }
      if (! builder->has_id)
        {
          free (fields);
          free_builders (builders, n_builders);
          return crun_make_error (err, 0, "missing required krun disk annotation krun.disk.%zu.id", index);
        }

      expected_index++;
    }

  *disks = calloc (n_builders, sizeof (**disks));
  if (*disks == NULL)
    {
      free (fields);
      free_builders (builders, n_builders);
      return crun_make_error (err, errno, "allocate krun disk configuration array");
    }
  for (i = 0; i < n_builders; i++)
    {
      (*disks)[i].path = builders[i].path;
      (*disks)[i].id = builders[i].id;
      (*disks)[i].readonly = builders[i].readonly;
      builders[i].path = NULL;
      builders[i].id = NULL;
    }
  *n_disks = n_builders;
  free_builders (builders, n_builders);
  free (fields);
  return 0;
}

static int
copy_json_string_field (yajl_val value, const char *field_name, size_t index, char **out, bool *found, libcrun_error_t *err)
{
  if (*found)
    return crun_make_error (err, 0, "duplicate krun disk JSON field `%s` for disk %zu", field_name, index);
  if (! YAJL_IS_STRING (value))
    return crun_make_error (err, 0, "krun disk JSON field `%s` for disk %zu must be a string", field_name, index);
  if (YAJL_GET_STRING (value)[0] == '\0')
    return crun_make_error (err, 0, "krun disk JSON field `%s` for disk %zu must not be empty", field_name, index);

  *out = dup_string (YAJL_GET_STRING (value), err);
  if (UNLIKELY (*out == NULL))
    return -1;
  *found = true;
  return 0;
}

static int
parse_json_disk (yajl_val disk, size_t index, struct krun_disk_config_s *out, libcrun_error_t *err)
{
  bool has_path = false;
  bool has_id = false;
  bool has_readonly = false;
  size_t i;

  if (! YAJL_IS_OBJECT (disk))
    return crun_make_error (err, 0, "krun disk JSON entry %zu must be an object", index);

  for (i = 0; i < disk->u.object.len; i++)
    {
      const char *key = disk->u.object.keys[i];
      yajl_val value = disk->u.object.values[i];
      int ret;

      if (strcmp (key, "path") == 0)
        {
          ret = copy_json_string_field (value, key, index, &out->path, &has_path, err);
          if (UNLIKELY (ret < 0))
            return ret;
        }
      else if (strcmp (key, "id") == 0)
        {
          ret = copy_json_string_field (value, key, index, &out->id, &has_id, err);
          if (UNLIKELY (ret < 0))
            return ret;
        }
      else if (strcmp (key, "readonly") == 0)
        {
          if (has_readonly)
            return crun_make_error (err, 0, "duplicate krun disk JSON field `readonly` for disk %zu", index);
          if (YAJL_IS_TRUE (value))
            out->readonly = true;
          else if (YAJL_IS_FALSE (value))
            out->readonly = false;
          else
            return crun_make_error (err, 0, "krun disk JSON field `readonly` for disk %zu must be a boolean", index);
          has_readonly = true;
        }
      else
        return crun_make_error (err, 0, "unsupported krun disk JSON field `%s` for disk %zu", key, index);
    }

  if (! has_path)
    return crun_make_error (err, 0, "missing required krun disk JSON field `path` for disk %zu", index);
  if (! has_id)
    return crun_make_error (err, 0, "missing required krun disk JSON field `id` for disk %zu", index);

  return 0;
}

static int
parse_json_disks (yajl_val config_tree, struct krun_disk_config_s **disks, size_t *n_disks, libcrun_error_t *err)
{
  const char *path_disks[] = { "disks", (const char *) 0 };
  yajl_val val_disks;
  size_t len;
  size_t i;

  if (config_tree == NULL)
    return 0;

  val_disks = yajl_tree_get (config_tree, path_disks, yajl_t_any);
  if (val_disks == NULL)
    return 0;

  if (! YAJL_IS_ARRAY (val_disks))
    return crun_make_error (err, 0, "krun VM configuration field `disks` must be an array");

  len = YAJL_GET_ARRAY (val_disks)->len;
  if (len == 0)
    return 0;

  *disks = calloc (len, sizeof (**disks));
  if (*disks == NULL)
    return crun_make_error (err, errno, "allocate krun disk JSON configuration array");
  *n_disks = len;

  for (i = 0; i < len; i++)
    {
      int ret = parse_json_disk (YAJL_GET_ARRAY (val_disks)->values[i], i, &(*disks)[i], err);
      if (UNLIKELY (ret < 0))
        {
          krun_free_disk_configs (*disks, *n_disks);
          *disks = NULL;
          *n_disks = 0;
          return ret;
        }
    }

  return 0;
}

void
krun_free_disk_configs (struct krun_disk_config_s *disks, size_t n_disks)
{
  size_t i;

  if (disks == NULL)
    return;

  for (i = 0; i < n_disks; i++)
    {
      free (disks[i].path);
      free (disks[i].id);
    }
  free (disks);
}

int
krun_parse_disk_configs (string_map *annotations, yajl_val config_tree,
                         struct krun_disk_config_s **disks, size_t *n_disks,
                         libcrun_error_t *err)
{
  bool found_annotations;
  int ret;

  if (disks == NULL || n_disks == NULL)
    return crun_make_error (err, EINVAL, "internal error: invalid krun disk parser output argument");

  *disks = NULL;
  *n_disks = 0;

  ret = parse_disk_annotations (annotations, disks, n_disks, &found_annotations, err);
  if (UNLIKELY (ret < 0))
    return ret;

  if (found_annotations)
    return 0;

  return parse_json_disks (config_tree, disks, n_disks, err);
}
