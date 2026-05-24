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

#include <libcrun/error.h>
#include <libcrun/handlers/krun-disk.h>
#include <libcrun/string_map.h>
#include <ocispec/runtime_spec_schema_config_schema.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*test) ();

struct annotation_s
{
  const char *key;
  const char *value;
};

static string_map *
make_annotations (const struct annotation_s *annotations, size_t n_annotations)
{
  json_map_string_string map = { 0 };
  char **keys = NULL;
  char **values = NULL;
  string_map *ret;
  size_t i;

  if (n_annotations > 0)
    {
      keys = calloc (n_annotations, sizeof (*keys));
      values = calloc (n_annotations, sizeof (*values));
      if (keys == NULL || values == NULL)
        abort ();
    }

  for (i = 0; i < n_annotations; i++)
    {
      keys[i] = (char *) annotations[i].key;
      values[i] = (char *) annotations[i].value;
    }

  map.keys = keys;
  map.values = values;
  map.len = n_annotations;
  ret = make_string_map_from_json (&map);
  free (keys);
  free (values);
  return ret;
}

static int
parse_config (const struct annotation_s *annotations, size_t n_annotations, const char *json,
              struct krun_disk_config_s **disks, size_t *n_disks, libcrun_error_t *err)
{
  string_map *map = make_annotations (annotations, n_annotations);
  json_object *tree = NULL;
  int ret;

  if (json != NULL)
    {
      tree = json_tokener_parse (json);
      if (tree == NULL)
        abort ();
    }

  ret = krun_parse_disk_configs (map, tree, disks, n_disks, err);
  free_string_map (map);
  json_object_put (tree);
  return ret;
}

static int
expect_failure (const struct annotation_s *annotations, size_t n_annotations, const char *json)
{
  struct krun_disk_config_s *disks = NULL;
  size_t n_disks = 0;
  libcrun_error_t err = NULL;
  int ret;

  ret = parse_config (annotations, n_annotations, json, &disks, &n_disks, &err);
  krun_free_disk_configs (disks, n_disks);
  if (ret >= 0)
    return -1;

  crun_error_release (&err);
  return 0;
}

static int
expect_no_disks (const struct annotation_s *annotations, size_t n_annotations, const char *json)
{
  struct krun_disk_config_s *disks = NULL;
  size_t n_disks = 0;
  libcrun_error_t err = NULL;
  int ret;

  ret = parse_config (annotations, n_annotations, json, &disks, &n_disks, &err);
  if (ret < 0)
    {
      crun_error_release (&err);
      return -1;
    }
  if (n_disks != 0 || disks != NULL)
    {
      krun_free_disk_configs (disks, n_disks);
      return -1;
    }

  return 0;
}

static int
test_annotation_single_default_readonly ()
{
  const struct annotation_s annotations[] = {
    { "krun.disk.0.path", "/path/to/nix.raw" },
    { "krun.disk.0.id", "nix" },
  };
  struct krun_disk_config_s *disks = NULL;
  size_t n_disks = 0;
  libcrun_error_t err = NULL;
  int ret;

  ret = parse_config (annotations, 2, NULL, &disks, &n_disks, &err);
  if (ret < 0)
    {
      crun_error_release (&err);
      return -1;
    }

  ret = n_disks == 1 && strcmp (disks[0].path, "/path/to/nix.raw") == 0 && strcmp (disks[0].id, "nix") == 0 && ! disks[0].readonly ? 0 : -1;
  krun_free_disk_configs (disks, n_disks);
  return ret;
}

static int
test_annotation_multiple_and_readonly ()
{
  const struct annotation_s annotations[] = {
    { "krun.disk.0.path", "/a.raw" },
    { "krun.disk.0.id", "a" },
    { "krun.disk.0.readonly", "false" },
    { "krun.disk.1.path", "/b.raw" },
    { "krun.disk.1.id", "b" },
    { "krun.disk.1.readonly", "true" },
  };
  struct krun_disk_config_s *disks = NULL;
  size_t n_disks = 0;
  libcrun_error_t err = NULL;
  int ret;

  ret = parse_config (annotations, 6, NULL, &disks, &n_disks, &err);
  if (ret < 0)
    {
      crun_error_release (&err);
      return -1;
    }

  ret = n_disks == 2 && strcmp (disks[0].id, "a") == 0 && ! disks[0].readonly && strcmp (disks[1].id, "b") == 0 && disks[1].readonly ? 0 : -1;
  krun_free_disk_configs (disks, n_disks);
  return ret;
}

static int
test_annotation_overrides_json ()
{
  const struct annotation_s annotations[] = {
    { "krun.disk.0.path", "/annotation.raw" },
    { "krun.disk.0.id", "annotation" },
  };
  const char *json = "{\"disks\":[{\"path\":\"/json.raw\",\"id\":\"json\",\"readonly\":true}]}";
  struct krun_disk_config_s *disks = NULL;
  size_t n_disks = 0;
  libcrun_error_t err = NULL;
  int ret;

  ret = parse_config (annotations, 2, json, &disks, &n_disks, &err);
  if (ret < 0)
    {
      crun_error_release (&err);
      return -1;
    }

  ret = n_disks == 1 && strcmp (disks[0].id, "annotation") == 0 && strcmp (disks[0].path, "/annotation.raw") == 0 && ! disks[0].readonly ? 0 : -1;
  krun_free_disk_configs (disks, n_disks);
  return ret;
}

static int
test_annotation_invalid_readonly ()
{
  const char *values[] = { "1", "0", "yes", "True", "FALSE", "", " true ", NULL };
  size_t i;

  for (i = 0; values[i] != NULL; i++)
    {
      const struct annotation_s annotations[] = {
        { "krun.disk.0.path", "/disk.raw" },
        { "krun.disk.0.id", "disk" },
        { "krun.disk.0.readonly", values[i] },
      };
      if (expect_failure (annotations, 3, NULL) < 0)
        return -1;
    }

  return 0;
}

static int
test_annotation_invalid_indexes ()
{
  const char *keys[] = {
    "krun.disk.1.path",
    "krun.disk.01.path",
    "krun.disk.+1.path",
    "krun.disk.-1.path",
    "krun.disk.a.path",
    "krun.disk..path",
    NULL,
  };
  size_t i;

  for (i = 0; keys[i] != NULL; i++)
    {
      const struct annotation_s annotations[] = {
        { keys[i], "/disk.raw" },
        { "krun.disk.0.id", "disk" },
      };
      if (expect_failure (annotations, 2, NULL) < 0)
        return -1;
    }

  return 0;
}

static int
test_annotation_missing_empty_and_unsupported ()
{
  const struct annotation_s missing_path[] = {
    { "krun.disk.0.id", "disk" },
  };
  const struct annotation_s missing_id[] = {
    { "krun.disk.0.path", "/disk.raw" },
  };
  const struct annotation_s empty_path[] = {
    { "krun.disk.0.path", "" },
    { "krun.disk.0.id", "disk" },
  };
  const struct annotation_s empty_id[] = {
    { "krun.disk.0.path", "/disk.raw" },
    { "krun.disk.0.id", "" },
  };
  const struct annotation_s unsupported[] = {
    { "krun.disk.0.path", "/disk.raw" },
    { "krun.disk.0.id", "disk" },
    { "krun.disk.0.format", "raw" },
  };

  if (expect_failure (missing_path, 1, NULL) < 0)
    return -1;
  if (expect_failure (missing_id, 1, NULL) < 0)
    return -1;
  if (expect_failure (empty_path, 2, NULL) < 0)
    return -1;
  if (expect_failure (empty_id, 2, NULL) < 0)
    return -1;
  if (expect_failure (unsupported, 3, NULL) < 0)
    return -1;

  return 0;
}

static int
test_json_success_and_top_level_keys ()
{
  const char *json = "{\"cpus\":2,\"ram_mib\":2048,\"disks\":[{\"path\":\"/nix.raw\",\"id\":\"nix\"},{\"path\":\"/ro.raw\",\"id\":\"ro\",\"readonly\":true}]}";
  struct krun_disk_config_s *disks = NULL;
  size_t n_disks = 0;
  libcrun_error_t err = NULL;
  int ret;

  if (expect_no_disks (NULL, 0, "{\"cpus\":2}") < 0)
    return -1;

  ret = parse_config (NULL, 0, json, &disks, &n_disks, &err);
  if (ret < 0)
    {
      crun_error_release (&err);
      return -1;
    }

  ret = n_disks == 2 && strcmp (disks[0].path, "/nix.raw") == 0 && strcmp (disks[0].id, "nix") == 0 && ! disks[0].readonly && strcmp (disks[1].id, "ro") == 0 && disks[1].readonly ? 0 : -1;
  krun_free_disk_configs (disks, n_disks);
  return ret;
}

static int
test_json_failures ()
{
  const char *jsons[] = {
    "{\"disks\":{}}",
    "{\"disks\":[1]}",
    "{\"disks\":[{\"id\":\"disk\"}]}",
    "{\"disks\":[{\"path\":\"/disk.raw\"}]}",
    "{\"disks\":[{\"path\":\"\",\"id\":\"disk\"}]}",
    "{\"disks\":[{\"path\":\"/disk.raw\",\"id\":\"\"}]}",
    "{\"disks\":[{\"path\":\"/disk.raw\",\"id\":\"disk\",\"readonly\":\"true\"}]}",
    "{\"disks\":[{\"path\":\"/disk.raw\",\"id\":\"disk\",\"readonly\":1}]}",
    "{\"disks\":[{\"path\":\"/disk.raw\",\"id\":\"disk\",\"format\":\"raw\"}]}",
    NULL,
  };
  size_t i;

  for (i = 0; jsons[i] != NULL; i++)
    if (expect_failure (NULL, 0, jsons[i]) < 0)
      return -1;

  return 0;
}

static int
test_duplicate_fields ()
{
  const struct annotation_s duplicate_annotations[] = {
    { "krun.disk.0.path", "/a.raw" },
    { "krun.disk.0.path", "/b.raw" },
    { "krun.disk.0.id", "disk" },
  };
  if (expect_failure (duplicate_annotations, 3, NULL) < 0)
    return -1;

  return 0;
}

static void
run_and_print_test_result (const char *name, int id, test t)
{
  int ret = t ();
  if (ret == 0)
    printf ("ok %d - %s\n", id, name);
  else if (ret == 77)
    printf ("ok %d - %s #SKIP\n", id, name);
  else
    printf ("not ok %d - %s\n", id, name);
}

#define RUN_TEST(T)                            \
  do                                           \
    {                                          \
      run_and_print_test_result (#T, id++, T); \
  } while (0)

int
main ()
{
  int id = 1;

  printf ("1..9\n");
  RUN_TEST (test_annotation_single_default_readonly);
  RUN_TEST (test_annotation_multiple_and_readonly);
  RUN_TEST (test_annotation_overrides_json);
  RUN_TEST (test_annotation_invalid_readonly);
  RUN_TEST (test_annotation_invalid_indexes);
  RUN_TEST (test_annotation_missing_empty_and_unsupported);
  RUN_TEST (test_json_success_and_top_level_keys);
  RUN_TEST (test_json_failures);
  RUN_TEST (test_duplicate_fields);

  return 0;
}
