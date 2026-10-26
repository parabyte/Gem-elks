#ifndef GEM_CONFIG_H
#define GEM_CONFIG_H

#ifndef GEM_CONFIG_FILE
#define GEM_CONFIG_FILE "/etc/gem.conf"
#endif

/* keep the two 64k code banks balanced */
#ifdef __ia16__
#define GEM_CONFIG_COLD __far __attribute__((far_section, noinline, \
	section(".fartext.gemconfig")))
#define GEM_CONFIG_NEAR __far __attribute__((near_section, noinline, \
	section(".text.gemconfig")))
#else
#define GEM_CONFIG_COLD
#define GEM_CONFIG_NEAR
#endif

int GEM_CONFIG_COLD gem_config_load(void);
const char * GEM_CONFIG_NEAR gem_config_get(const char *name);

#endif
