"""VM-only native UI regression; requires a disposable knowledge fixture and PID."""
import os, sys, time, json, subprocess
from pathlib import Path
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi

pid = int(os.environ["NEXTNOTES_TEST_PID"])
library = Path(os.environ["NEXTNOTES_TEST_LIBRARY"])
env = os.environ.copy()
env.update(XDG_RUNTIME_DIR="/run/user/1000", WAYLAND_DISPLAY="wayland-0",
           YDOTOOL_SOCKET="/run/user/1000/.ydotool_socket")

def walk(a):
    if a is None: return
    yield a
    if a.get_role_name() == "document web": return
    for i in range(a.get_child_count()): yield from walk(a.get_child_at_index(i))

def items():
    d = Atspi.get_desktop(0)
    app = next(d.get_child_at_index(i) for i in range(d.get_child_count())
               if d.get_child_at_index(i).get_process_id() == pid)
    return [a for a in walk(app) if a.get_state_set().contains(Atspi.StateType.SHOWING)]

def find(role, name):
    hits = [a for a in items() if a.get_role_name() == role and a.get_name() == name]
    assert len(hits) == 1, (role, name, len(hits))
    return hits[0]

def press(role, name):
    a = find(role, name).get_action_iface()
    names = [a.get_action_name(i).lower() for i in range(a.get_n_actions())]
    assert a.do_action(names.index("press") if "press" in names else 0)
    time.sleep(.8)

def fill(name, text):
    for attempt in range(3):
        assert find("text", name).get_component_iface().grab_focus()
        time.sleep(.2)
        subprocess.run(["wl-copy"], input=text.encode(), env=env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(.2)
        subprocess.run(["ydotool", "key", "29:1", "30:1", "30:0", "29:0",
                        "29:1", "47:1", "47:0", "29:0"], env=env, check=True)
        time.sleep(.4)
        actual = Atspi.Text.get_text(find("text", name), 0, -1).replace("\u2029", "\n")
        if actual == text: return
    raise AssertionError((name, text, actual))

def until(fn):
    end = time.monotonic() + 10
    while time.monotonic() < end:
        if fn(): return
        time.sleep(.1)
    raise AssertionError("timed out")

def state(): return json.loads((library / ".nextnotes/state.json").read_text())
def menu(label):
    press("button", "更多笔记操作")
    press("menu item", label)

subprocess.run(["python3", "/home/amao/apps/nk-vm-window.py", str(pid)], check=True)
menu("笔记模板…")
fill("模板名称", "每日笔记")
fill("新笔记标题", "Template title")
fill("模板 Markdown 内容", "# {{title}}\n\n{{date}} {{time}}\nDaily marker\n")
press("button", "保存模板")
until(lambda: "每日笔记" in state().get("templates", {}))
before = set(library.rglob("*.md"))
press("button", "从模板新建")
until(lambda: any(p.name.startswith("笔记-") for p in set(library.rglob("*.md")) - before))
created = next(p for p in set(library.rglob("*.md")) - before if p.name.startswith("笔记-"))
assert "# Template title" in created.read_text() and "{{date}}" not in created.read_text()
print("native_template_save_create_pass", flush=True)
menu("打开今日笔记")
daily = library / "日记" / (time.strftime("%Y-%m-%d") + ".md")
until(daily.exists)
original = daily.read_bytes()
assert b"Daily marker" in original
menu("打开今日笔记")
assert daily.read_bytes() == original
print("native_daily_template_idempotent_pass", flush=True)
menu("标签与属性…")
fill("笔记标签", "工作, project alpha")
press("button", "保存标签")
until(lambda: "project alpha" in daily.read_text())
fill("属性名，例如 status、project、date", "status")
fill("属性值", "done")
press("button", "保存属性")
until(lambda: "status: 'done'" in daily.read_text())
press("button", "历史版本")
until(lambda: any(a.get_role_name() == "button" and " · 当前版本" in a.get_name() for a in items()))
versions = [a for a in items() if a.get_role_name() == "button"
            and a.get_name().startswith("20") and " · 当前版本" not in a.get_name()]
assert versions
versions[-1].get_action_iface().do_action(0)
time.sleep(.4)
press("button", "恢复所选版本…")
press("button", "确定")
until(lambda: "status:" not in daily.read_text() and "project alpha" not in daily.read_text())
time.sleep(.8)
press("button", "撤销")
until(lambda: "status: 'done'" in daily.read_text())
print("native_properties_history_restore_undo_pass", flush=True)
time.sleep(.8)
menu("切换到正文模式")
menu("链接与反向链接…")
fill("搜索要链接的笔记", "Beta")
press("button", "Beta · b.md")
press("button", "插入内部链接")
until(lambda: "[[../b.md|Beta]]" in daily.read_text())
print("native_link_picker_insert_pass", flush=True)
fill("搜索所有笔记", 'tag:"project alpha"')
time.sleep(.5)
menu("保存搜索为智能文件夹…")
fill("", "Work query")
press("button", "确定")
until(lambda: state().get("smartFolders", {}).get("Work query", {}).get("query") == 'tag:"project alpha"')
fill("搜索所有笔记", "no-such-note")
press("button", "Work query")
assert Atspi.Text.get_text(find("text", "搜索所有笔记"), 0, -1) == 'tag:"project alpha"'
print("native_smart_folder_save_open_pass", flush=True)
