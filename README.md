Replacement-ish for update-mime-database from shared-mime-info.

Because that uses glib for some idiotic reason and is absurdly slow (uses ~10
seconds here, this uses less than 100ms).
