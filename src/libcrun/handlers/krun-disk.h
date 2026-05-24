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
#ifndef KRUN_DISK_H
#define KRUN_DISK_H

#include <config.h>
#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

#include "../error.h"
#include "../string_map.h"

struct krun_disk_config_s
{
  char *path;
  char *id;
  bool readonly;
};

void krun_free_disk_configs (struct krun_disk_config_s *disks, size_t n_disks);

int krun_parse_disk_configs (string_map *annotations, json_object *config_tree,
                             struct krun_disk_config_s **disks, size_t *n_disks,
                             libcrun_error_t *err);

#endif
