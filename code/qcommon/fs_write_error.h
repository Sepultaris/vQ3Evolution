/* Included by files.c. Capture the original failure before diagnostic I/O can
 * overwrite errno. No settings, access rights or fallback save paths change. */
static void FS_ReportWriteError(const char *path, int error, unsigned long osError)
{
    static qboolean reporting;
    char message[MAX_OSPATH+256], logPath[MAX_OSPATH];
    FILE *log;
    if (reporting) return; // Com_Printf may itself try to open qconsole.log.
    reporting=qtrue;
    Com_sprintf(message,sizeof(message),
        "Filesystem write failed: %s (errno %d: %s; OS error %lu)\n",
        path,error,strerror(error),osError);
    Com_Printf("%s",message);

    // Per-game writes can fail while home-root writes still work. Append an
    // engine-owned diagnostic there so users need not copy console text.
    Q_strncpyz(logPath,FS_BuildOSPath(fs_homepath->string,"filesystem-write-errors.log",""),sizeof(logPath));
    logPath[strlen(logPath)-1]=0;
    log=Sys_FOpen(logPath,"ab");
    if (log) {
        fputs(message,log);
        fclose(log);
    }
    reporting=qfalse;
}
