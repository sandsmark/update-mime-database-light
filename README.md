update-mime-database Light Edition™
===================================

![logo](/light-mime.jpg)

Replacement for update-mime-database from
[shared-mime-info](https://freedesktop.org/wiki/Software/shared-mime-info/).

Because that uses glib for some idiotic reason and is absurdly slow (uses ~10
seconds here, this uses less than 100ms).

Part of my crusade against glib, saving the environment by replacing crappy C code.
