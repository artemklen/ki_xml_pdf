#ifndef KI_PLATFORM_H
#define KI_PLATFORM_H

/* Include after any feature-test macro. features.h sets __GLIBC__ on glibc. */
#include <features.h>
#if !defined(__linux__) || !defined(__GLIBC__)
#error "ki_xml_pdf supports only Linux with glibc; see README 'Supported platforms'"
#endif

#endif
