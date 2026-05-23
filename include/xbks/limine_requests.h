#ifndef XBKS_LIMINE_REQUESTS_H
#define XBKS_LIMINE_REQUESTS_H

#include <limine.h>
#include <xbks/boot_info.h>
#include <xbks/types.h>

const struct xbks_boot_info *xbks_limine_boot_info(void);
bool xbks_limine_base_revision_supported(void);

#endif
