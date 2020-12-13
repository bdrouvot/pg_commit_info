# contrib/bdt_decoding/Makefile

MODULES = pg_commit_info
PGFILEDESC = "pg_commit_info - A logical decoding output plugin that provides commits information"

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
