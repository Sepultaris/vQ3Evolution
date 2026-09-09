"""Read-only acceptance/summary of a local Nsight Systems SQLite export.

Hardware counters are device-wide. Range averages are diagnostic evidence,
not causal attribution, an FPS benchmark, or proof a shader is memory-bound.
"""
import argparse
import json
from pathlib import Path
import sqlite3


def inspect(path, require_labels=False):
    connection = sqlite3.connect(Path(path).resolve().as_uri() + '?mode=ro', uri=True)
    with connection as c:
        tables = {r[0] for r in c.execute("select name from sqlite_master where type='table'")}
        required = {'GPU_METRICS', 'TARGET_INFO_GPU_METRICS', 'VULKAN_API', 'VULKAN_WORKLOAD', 'PROCESSES', 'StringIds'}
        if not required <= tables:
            raise ValueError('Missing capture data: ' + ', '.join(sorted(required - tables)))
        applications = c.execute("select globalPid,pid from PROCESSES where lower(name)='vq3evolution.exe'").fetchall()
        if len(applications) != 1:
            raise ValueError('Expected exactly one profiled game process')
        global_pid, pid = applications[0]
        api_rows = c.execute('select count(*) from VULKAN_API where (globalTid & -16777216)=?', (global_pid,)).fetchone()[0]
        workloads = c.execute('select count(*) from VULKAN_WORKLOAD where end>start').fetchone()[0]
        readings, instants = c.execute('select count(*),count(distinct timestamp) from GPU_METRICS where value is not null').fetchone()
        if not api_rows or not workloads or instants < 100:
            raise ValueError('Insufficient game API, GPU workload or hardware counter data')
        stages = c.execute("""select s.value,count(*),avg(w.end-w.start)/1e6
            from VULKAN_WORKLOAD w join StringIds s on s.id=w.textId
            where s.value like 'VQ3E %' and w.end>w.start group by s.value""").fetchall()
        if require_labels and not any(r[0] == 'VQ3E Path tracing' for r in stages):
            raise ValueError('No labeled path-tracing GPU ranges captured')
        names = [
            'GR Engine Active [Throughput %]', 'VRAM Total Bandwidth [Throughput %]',
            'SM Throughput [Throughput %]', 'L1TEX Throughput [Throughput %]', 'RTCORE Throughput [Throughput %]',
            'SM Issue Stage Throughput [Throughput %]', 'Sync CS SM Warps [Occupancy %]',
            'CS Register Allocation (Sync) [Occupancy %]',
        ]
        counters = c.execute("""select i.metricName,count(*),avg(g.value),max(g.value)
            from GPU_METRICS g join TARGET_INFO_GPU_METRICS i on i.typeId=g.typeId and i.metricId=g.metricId
            where i.metricName in (""" + ','.join('?' for _ in names) + ') group by i.metricName', names).fetchall()
        warnings = c.execute('select text,globalPid from DIAGNOSTIC_EVENT where severity>1').fetchall() if 'DIAGNOSTIC_EVENT' in tables else []
        return dict(verified=True, gamePid=pid, gameVulkanApiEvents=api_rows, gpuWorkloads=workloads,
                    hardwareReadings=readings, counterSampleInstants=instants,
                    stages=[dict(name=n, ranges=k, meanAnnotationSpanMilliseconds=ms) for n, k, ms in stages],
                    wholeCaptureDeviceCounters=[dict(name=n, samples=k, mean=avg, maximum=high) for n, k, avg, high in counters],
                    warnings=[dict(text=t, gameProcess=p == global_pid) for t, p in warnings],
                    caveat='Device-wide counters; whole-capture averages mix stages. Annotation spans are not necessarily execution-complete GPU timings; near-zero annotations do not establish negligible cost. Not an FPS result or shader-stall diagnosis.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sqlite_export')
    parser.add_argument('--require-labels', action='store_true')
    args = parser.parse_args()
    print(json.dumps(inspect(args.sqlite_export, args.require_labels), indent=2))
