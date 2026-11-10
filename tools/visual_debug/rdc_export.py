# ffxiii2-nx (2026-11-06, D-003 on the PC): exports what each RenderDoc capture presented, plus its action list.
# Run with RenderDoc's own Python (qrenderdoc embeds 3.8):
#   set RDC_DIR=<folder with .rdc files>& set RDC_OUT=<output folder>& qrenderdoc.exe --python rdc_export.py
# Optional: RDC_EIDS=<capture name>:<eid>,<eid>,... also saves every colour output of those events (one capture).
# For each capture: <name>.png = the output of the last draw before the present (Renut's scan-out into the
# swapchain image, i.e. the picture shown), <name>_actions.txt = every action (event id, name, its colour outputs and
# depth target). Nothing in the capture is changed.
import os
import sys
import traceback

_trace = open(os.path.join(os.environ.get('RDC_OUT') or os.environ.get('TEMP', '.'), 'rdc_export_trace.txt'), 'w')
_trace.write('started, RDC_DIR=%s RDC_OUT=%s\n' % (os.environ.get('RDC_DIR'), os.environ.get('RDC_OUT')))
_trace.flush()
try:
    import renderdoc as rd
except Exception:
    _trace.write(traceback.format_exc())
    _trace.close()
    raise
_trace.write('renderdoc imported\n')
_trace.close()

src = os.environ.get('RDC_DIR', '')
out = os.environ.get('RDC_OUT', src)
eids_spec = os.environ.get('RDC_EIDS', '')
os.makedirs(out, exist_ok=True)
report = open(os.path.join(out, 'rdc_export_report.txt'), 'w')


def walk(actions, acc):
    for a in actions:
        acc.append(a)
        walk(a.children, acc)
    return acc


def save(ctrl, rid, path):
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.destType = rd.FileType.PNG
    ts.mip = 0
    ts.slice.sliceIndex = 0
    ts.alpha = rd.AlphaMapping.Discard
    return ctrl.SaveTexture(ts, path)


def one(path, want_eids):
    name = os.path.splitext(os.path.basename(path))[0]
    cap = rd.OpenCaptureFile()
    res = cap.OpenFile(path, '', None)
    if res != rd.ResultCode.Succeeded:
        report.write('%s: open failed %s\n' % (name, res))
        return
    res, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    if res != rd.ResultCode.Succeeded:
        report.write('%s: replay failed %s\n' % (name, res))
        cap.Shutdown()
        return
    sf = ctrl.GetStructuredFile()
    acts = walk(ctrl.GetRootActions(), [])
    with open(os.path.join(out, name + '_actions.txt'), 'w') as f:
        for a in acts:
            outs = [str(int(o)) for o in a.outputs if o != rd.ResourceId.Null()]
            f.write('%d\t%s\tout=%s\tdepth=%s\tflags=%s\n' % (a.eventId, a.GetName(sf), ','.join(outs),
                                                         int(a.depthOut) if a.depthOut != rd.ResourceId.Null() else '-',
                                                         str(a.flags)))
    with open(os.path.join(out, name + '_textures.txt'), 'w') as f:
        for t in ctrl.GetTextures():
            f.write('%d\t%dx%d\tfmt=%s\tmsaa=%d\n' % (int(t.resourceId), t.width, t.height, t.format.Name(), t.msSamp))
    last = None
    for a in acts:
        if a.flags & rd.ActionFlags.Present:
            break
        if (a.flags & rd.ActionFlags.Drawcall) and a.outputs[0] != rd.ResourceId.Null():
            last = a
    if last is not None:
        ctrl.SetFrameEvent(last.eventId, True)
        ok = save(ctrl, last.outputs[0], os.path.join(out, name + '.png'))
        report.write('%s: %d actions, shown = eid %d output %d saved %s\n' % (name, len(acts), last.eventId,
                                                                          int(last.outputs[0]), ok))
    else:
        report.write('%s: %d actions, no draw before the present\n' % (name, len(acts)))
    for eid in want_eids:
        ctrl.SetFrameEvent(eid, True)
        a = ctrl.GetAction(eid)
        if a is None:
            continue
        for i, o in enumerate(a.outputs):
            if o != rd.ResourceId.Null():
                save(ctrl, o, os.path.join(out, '%s_e%d_o%d_r%d.png' % (name, eid, i, int(o))))
    ctrl.Shutdown()
    cap.Shutdown()


want = {}
if eids_spec:
    cname, _, lst = eids_spec.partition(':')
    want[cname] = [int(x) for x in lst.split(',') if x]
try:
    for fn in sorted(os.listdir(src)):
        if fn.endswith('.rdc'):
            try:
                one(os.path.join(src, fn), want.get(os.path.splitext(fn)[0], []))
            except Exception:
                report.write('%s: %s\n' % (fn, traceback.format_exc()))
            report.flush()
finally:
    report.write('done\n')
    report.close()
    os._exit(0)
