' 解决中文乱码核心：必须保存为 ANSI 编码（不要用 UTF-8！）
Set ws = CreateObject("Wscript.Shell")

' 程序路径
exePath = ws.CurrentDirectory & "\Bin\SuLiao.exe"

' 当前路径 + 中文名称
linkPath = ws.CurrentDirectory & "\速聊.lnk"

' 创建快捷方式
Set shortcut = ws.CreateShortcut(linkPath)
shortcut.TargetPath = exePath
shortcut.WorkingDirectory = ws.CurrentDirectory & "\Bin"
shortcut.IconLocation = exePath & ",0"
shortcut.Save
