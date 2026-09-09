/* Local VM lookup policy. Included after the filesystem's search-path types.
 * Prefer loose source-built modules WITHIN a game, never across game tiers.
 * Pure remote lookup continues to use the original approved-pack path.
 * GPL-2.0-or-later. */
static int FS_FindLocalVM(void **startSearch, char *found, int foundlen,
    const char *dllName, const char *qvmName, const char *const games[3])
{
    searchpath_t *search, *last = *startSearch;
    const char *lastGame = last ? (last->dir ? last->dir->gamedir : last->pack->pakGamename) : NULL;
    int game, previous;
    for (game=0; game<3; ++game)
    {
        if (!games[game] || !*games[game]) continue;
        for (previous=0; previous<game; ++previous)
            if (games[previous] && !FS_FilenameCompare(games[previous],games[game])) break;
        if (previous<game) continue;
        if (lastGame && FS_FilenameCompare(lastGame,games[game])) continue;

        // On a failed native load, resume after that directory; on a failed
        // QVM load, do not retry directories or older packs from its path.
        if (!last || last->dir)
        {
            for (search=last ? last->next : fs_searchpaths; search; search=search->next)
            {
                directory_t *dir=search->dir;
                if (!dir || FS_FilenameCompare(dir->gamedir,games[game])) continue;
                if (dllName)
                {
                    const char *path=FS_BuildOSPath(dir->path,dir->gamedir,dllName);
                    if (FS_FileInPathExists(path))
                    {
                        Q_strncpyz(found,path,foundlen);
                        *startSearch=search;
                        return VMI_NATIVE;
                    }
                }
                if (FS_FOpenFileReadDir(qvmName,search,NULL,qfalse,qtrue)>0)
                {
                    *startSearch=search;
                    return VMI_COMPILED;
                }
            }
        }
        for (search=last && last->pack ? last->next : fs_searchpaths; search; search=search->next)
        {
            pack_t *pack=search->pack;
            if (!pack || FS_FilenameCompare(pack->pakGamename,games[game])) continue;
            if (last && last->pack && !FS_FilenameCompare(last->pack->pakPathname,pack->pakPathname)) continue;
            if (FS_FOpenFileReadDir(qvmName,search,NULL,qfalse,qtrue)>0)
            {
                *startSearch=search;
                return VMI_COMPILED;
            }
        }
        last=NULL;
        lastGame=NULL;
    }
    return -1;
}
