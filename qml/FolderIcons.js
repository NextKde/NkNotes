.pragma library

// App-specific subjects extend the shared 24px outline icon vocabulary.
var paths = {
    work: "M8 7V5a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2 M5 7h14a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2 M3 12a20 20 0 0 0 18 0 M12 11v4",
    study: "M12 5v16 M12 5C9 3 5 3 2 4v15c4-1 7-1 10 2 3-3 6-3 10-2V4c-3-1-7-1-10 1",
    sport: "M15 2a2 2 0 1 0 0 4 2 2 0 1 0 0-4 M5 10l4-3 4 2 3 4h4 M13 9l-3 5 4 3v5 M10 14l-3 5H3",
    running: "M3 6h5l2 6 4 2 5 1a3 3 0 0 1 2 3v2H3z M3 17h18 M10 12l-2 2 M13 14l-2 2",
    cycling: "M6 13a4 4 0 1 0 0 8 4 4 0 1 0 0-8 M18 13a4 4 0 1 0 0 8 4 4 0 1 0 0-8 M6 17l4-8 4 8H6 M10 9h7 M15 5h3l-1 4 1 8 M8 6h4",
    swimming: "M18 7a2 2 0 1 0 0 4 2 2 0 1 0 0-4 M3 12l6-6 5 3-4 5 M3 16q2-2 4 0t4 0 4 0 4 0 M3 21q2-2 4 0t4 0 4 0 4 0",
    fitness: "M5 6h3v12H5z M16 6h3v12h-3z M8 12h8 M2 9v6 M22 9v6 M2 12h3 M19 12h3",
    basketball: "M12 3a9 9 0 1 0 0 18 9 9 0 1 0 0-18 M3 12h18 M12 3v18 M6 5c7 4 7 10 0 14 M18 5c-7 4-7 10 0 14",
    football: "M12 3a9 9 0 1 0 0 18 9 9 0 1 0 0-18 M12 8l5 3-2 5H9l-2-5z M12 3v5 M17 11l4-2 M15 16l3 3 M9 16l-3 3 M7 11 3 9",
    badminton: "M4 4l6 14 4-1L21 4z M9 4l3 13 M15 4l-2 13 M7 11h10 M10 18l1 3h3l1-4",
    tennis: "M14 3c-4 0-7 3-7 7s2 6 5 6 7-4 7-8-2-5-5-5z M9 15l-6 6 M10 5l6 8 M7 9l6 7 M9 12l8-7",
    shopping: "M5 7h14l2 14H3z M8 8V6a4 4 0 0 1 8 0v2",
    travel: "M22 2 9 15 M22 2l-5 20-5-10L2 7z",
    food: "M4 3v6a3 3 0 0 0 6 0V3 M7 3v18 M20 3c-4 3-5 7-5 10h5 M20 3v18",
    coffee: "M3 8h14v8a4 4 0 0 1-4 4H7a4 4 0 0 1-4-4z M17 9h2a3 3 0 0 1 0 6h-2 M6 3v2 M10 2v3 M14 3v2",
    health: "M9 3h6v6h6v6h-6v6H9v-6H3V9h6z",
    finance: "M20 8V5H5a3 3 0 0 0 0 6h16v10H5a3 3 0 0 1-3-3V8 M21 13h-5v5h5 M17 15.5h.01",
    gift: "M3 8h18v5H3z M5 13v8h14v-8 M12 8v13 M12 8H7a3 3 0 1 1 3-3z M12 8h5a3 3 0 1 0-3-3z",
    pets: "M8 14c2-4 6-4 8 0l3 4c2 4-3 5-7 3-4 2-9 1-7-3z M7 5a2 3 0 1 0 4 0 2 3 0 1 0-4 0 M14 5a2 3 0 1 0 4 0 2 3 0 1 0-4 0 M2 10a2 2 0 1 0 4 0 2 2 0 1 0-4 0 M19 10a2 2 0 1 0 4 0 2 2 0 1 0-4 0",
    garden: "M20 3C6 1 1 8 6 16c8 5 15 0 14-13 M3 21 15 9",
    car: "M4 11l2-6h12l2 6 M3 11h18v8H3z M5 19v2 M19 19v2 M7 15h.01 M17 15h.01",
    code: "M8 6 2 12l6 6 M16 6l6 6-6 6 M14 3l-4 18",
    camera: "M8 6l2-3h4l2 3h4a2 2 0 0 1 2 2v11H2V8a2 2 0 0 1 2-2z M8 12a4 4 0 1 0 8 0 4 4 0 1 0-8 0",
    ideas: "M8 16c0-3-3-4-3-8a7 7 0 0 1 14 0c0 4-3 5-3 8z M8 19h8 M10 22h4"
}

var choices = [
    { name: "folder", label: "文件夹" }, { name: "home", label: "家庭" },
    { name: "work", label: "工作" }, { name: "study", label: "学习" },
    { name: "sport", label: "运动" },
    { name: "running", label: "跑步" }, { name: "cycling", label: "骑行" },
    { name: "swimming", label: "游泳" }, { name: "fitness", label: "健身" },
    { name: "basketball", label: "篮球" }, { name: "football", label: "足球" },
    { name: "badminton", label: "羽毛球" }, { name: "tennis", label: "网球" },
    { name: "shopping", label: "购物" },
    { name: "travel", label: "旅行" }, { name: "food", label: "美食" },
    { name: "coffee", label: "咖啡" }, { name: "health", label: "健康" },
    { name: "finance", label: "财务" }, { name: "gift", label: "礼物" },
    { name: "pets", label: "宠物" }, { name: "garden", label: "园艺" },
    { name: "car", label: "汽车" }, { name: "code", label: "编程" },
    { name: "camera", label: "摄影" }, { name: "ideas", label: "想法" },
    { name: "editRounded", label: "书写" }, { name: "album", label: "笔记本" },
    { name: "flag", label: "旗帜" }, { name: "heart", label: "喜欢" },
    { name: "calendar", label: "日历" }, { name: "music", label: "音乐" },
    { name: "grid", label: "分类" }, { name: "sparkle", label: "灵感" },
    { name: "artist", label: "人物" }, { name: "reminder", label: "清单" },
    { name: "archive", label: "归档" }, { name: "envelope", label: "邮件" },
    { name: "wallpaper", label: "图片" }, { name: "keyboard", label: "键盘" },
    { name: "settings", label: "设置" }, { name: "queueList", label: "列表" },
    { name: "radio", label: "广播" }, { name: "theme", label: "主题" },
    { name: "sliders", label: "调节" }, { name: "circle", label: "圆环" },
    { name: "waves", label: "波浪" }, { name: "info", label: "资料" }
]

function source(name, color) {
    if (!paths[name]) return ""
    var ink = "rgb(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + "," + Math.round(color.b * 255) + ")"
    return "data:image/svg+xml;utf8," + encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="none" stroke="'
        + ink + '" stroke-opacity="' + color.a + '" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="'
        + paths[name] + '"/></svg>')
}
