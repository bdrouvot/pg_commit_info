/*-------------------------------------------------------------------------
 *
 * pg_commit_info.c
 *		 logical decoding output plugin to provides commit information
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "replication/logical.h"
#include "utils/memutils.h"
#include "utils/builtins.h"
#include "utils/rel.h"

PG_MODULE_MAGIC;

extern void _PG_init(void);
extern void _PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct
{
	MemoryContext context;
	uint64 		nb_insert;
	uint64 		nb_delete;
	uint64 		nb_update;
#if PG_VERSION_NUM >= 110000
	uint64 		nb_truncate;
	uint64 		nb_rel_truncated;
#endif
	bool 		skip_empty_xacts;
	bool		xact_wrote_changes;
} CommitInfoDecodingData;

static void pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt,
							  bool is_init);
static void pg_decode_shutdown(LogicalDecodingContext *ctx);
static void pg_decode_begin_txn(LogicalDecodingContext *ctx,
								ReorderBufferTXN *txn);
static void pg_decode_commit_txn(LogicalDecodingContext *ctx,
								 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change(LogicalDecodingContext *ctx,
							 ReorderBufferTXN *txn, Relation rel,
							 ReorderBufferChange *change);
#if PG_VERSION_NUM >= 110000
static void pg_decode_truncate(LogicalDecodingContext *ctx,
							   ReorderBufferTXN *txn,
							   int nrelations, Relation relations[],
							   ReorderBufferChange *change);
#endif

void
_PG_init(void)
{
}

/* specify output plugin callbacks */
void
_PG_output_plugin_init(OutputPluginCallbacks *cb)
{
	AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);

	cb->startup_cb = pg_decode_startup;
	cb->begin_cb = pg_decode_begin_txn;
	cb->change_cb = pg_decode_change;
#if PG_VERSION_NUM >= 110000
	cb->truncate_cb = pg_decode_truncate;
#endif
	cb->commit_cb = pg_decode_commit_txn;
	cb->shutdown_cb = pg_decode_shutdown;
}


/* initialize this plugin */
static void
pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt,
				  bool is_init)
{
	ListCell   *option;
	CommitInfoDecodingData *data;

	data = palloc0(sizeof(CommitInfoDecodingData));
	data->context = AllocSetContextCreate(ctx->context,
										  "text conversion context",
										  ALLOCSET_DEFAULT_SIZES);
	data->nb_insert = 0;
	data->nb_update = 0;
	data->nb_delete = 0;
#if PG_VERSION_NUM >= 110000
	data->nb_truncate = 0;
	data->nb_rel_truncated = 0;
#endif

	ctx->output_plugin_private = data;
	opt->output_type = OUTPUT_PLUGIN_TEXTUAL_OUTPUT;

	foreach(option, ctx->output_plugin_options)
	{
		DefElem    *elem = lfirst(option);

		Assert(elem->arg == NULL || IsA(elem->arg, String));

		if (strcmp(elem->defname, "skip-empty-xacts") == 0)
		{
			if (elem->arg == NULL)
				data->skip_empty_xacts = false;
			else if (!parse_bool(strVal(elem->arg), &data->skip_empty_xacts))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("could not parse value \"%s\" for parameter \"%s\"",
								strVal(elem->arg), elem->defname)));
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("option \"%s\" = \"%s\" is unknown",
							elem->defname,
							elem->arg ? strVal(elem->arg) : "(null)")));
		}
	}
}

/* cleanup this plugin's resources */
static void
pg_decode_shutdown(LogicalDecodingContext *ctx)
{
	CommitInfoDecodingData *data = ctx->output_plugin_private;

	/* cleanup our own resources via memory context reset */
	MemoryContextDelete(data->context);
}

/* BEGIN callback */
static void
pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	CommitInfoDecodingData *data = ctx->output_plugin_private;

	data->nb_insert = 0;
	data->nb_update = 0;
	data->nb_delete = 0;
#if PG_VERSION_NUM >= 110000
	data->nb_truncate = 0;
	data->nb_rel_truncated = 0;
#endif

	data->xact_wrote_changes = false;
}

/* COMMIT callback */
static void
pg_decode_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	CommitInfoDecodingData *data = ctx->output_plugin_private;

	if (data->skip_empty_xacts && !data->xact_wrote_changes)
		return;

	OutputPluginPrepareWrite(ctx, true);
#if PG_VERSION_NUM >= 110000
	appendStringInfo(ctx->out, "xid %u: lsn:%X/%08X inserts:%lu deletes:%lu updates:%lu truncates:%lu relations truncated:%lu"								,txn->xid
							,(uint32) (commit_lsn >> 32)
							,(uint32) commit_lsn
							,data->nb_insert
							,data->nb_delete
							,data->nb_update
							,data->nb_truncate
							,data->nb_rel_truncated);
#else
	appendStringInfo(ctx->out, "xid %u: lsn:%X/%08X inserts:%lu deletes:%lu updates:%lu", txn->xid
							,(uint32) (commit_lsn >> 32)
							,(uint32) commit_lsn
							,data->nb_insert
							,data->nb_delete
							,data->nb_update);
#endif
						
	OutputPluginWrite(ctx, true);
}

/*
 * callback for individual changed tuples
 */
static void
pg_decode_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	CommitInfoDecodingData *data;

	data = ctx->output_plugin_private;
	data->xact_wrote_changes = true;

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			data->nb_insert++;
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			data->nb_update++;
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			data->nb_delete++;
			break;
		default:
			Assert(false);
	}
}
#if PG_VERSION_NUM >= 110000
static void
pg_decode_truncate(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				   int nrelations, Relation relations[], ReorderBufferChange *change)
{
	CommitInfoDecodingData *data;

	data = ctx->output_plugin_private;
	data->xact_wrote_changes = true;
	data->nb_truncate++;
	data->nb_rel_truncated = nrelations;
}
#endif
