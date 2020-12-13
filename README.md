Introduction
============

**pg_commit_info** is an output plugin for logical decoding. It provides information about commits:

 * number of tuples inserted
 * number of tuples updated
 * number of tuples deleted
 * number of truncates (if version >= 11)
 * number of relation truncated (if version >= 11)
 * lsn 

Build and Install
=================

It can be built using the standard PGXS infrastructure. For this to
work, the ``pg_config`` program must be available in your $PATH. Instruction to
install follows:

    $ git clone
    $ cd pg_commit_info
    $ make
    $ make install

Configuration
=============

It has been tested on version >= 9.6.

postgresql.conf
---------------

```
wal_level = logical
# this one is needed for version 9.6
# default value are fine with version >= 10
max_replication_slots = 10
```

Parameter
----------

* `skip-empty-xacts`: skip empty  _xid_. Default is _false_.

Examples
========

```
$ cat example.sql
SELECT 'init' FROM pg_create_logical_replication_slot('test_commit_info', 'pg_commit_info');
create table relex (a int);
begin;
insert into relex values (1);
insert into relex values (1);
insert into relex values (1);
commit;
begin;
insert into relex values (1);
insert into relex values (1);
commit;
begin;
update relex set a = 2;
insert into relex values (1);
delete from relex where a = 2;
truncate relex;
end;
SELECT data FROM pg_logical_slot_get_changes('test_commit_info', NULL, NULL, 'skip-empty-xacts', '0');
DROP TABLE relex;
```

produces:

```
$ psql -At -f example.sql
init
CREATE TABLE
BEGIN
INSERT 0 1
INSERT 0 1
INSERT 0 1
COMMIT
BEGIN
INSERT 0 1
INSERT 0 1
COMMIT
BEGIN
UPDATE 5
INSERT 0 1
DELETE 5
TRUNCATE TABLE
COMMIT
xid 75548: lsn:1/3B091CE8 inserts:0 deletes:0 updates:0 truncates:0 relations truncated:0
xid 75549: lsn:1/3B091F78 inserts:3 deletes:0 updates:0 truncates:0 relations truncated:0
xid 75550: lsn:1/3B092040 inserts:2 deletes:0 updates:0 truncates:0 relations truncated:0
xid 75551: lsn:1/3B092588 inserts:1 deletes:5 updates:5 truncates:1 relations truncated:1
DROP TABLE
```

License
=======

pg_commit_info is free software distributed under the PostgreSQL license.

Copyright (c) 2020, Bertrand Drouvot.
