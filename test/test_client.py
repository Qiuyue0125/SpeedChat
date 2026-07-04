#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_client.py — Chat 服务端测试脚本

协议帧头与版本常量须与 ``user/depend/SocketDepend.h``、``server/depend/SocketDepend.h`` 对齐。

================================================================================
【各测试项说明】测什么、用什么接口、有什么用
================================================================================

  1. full - 完整流程
     测什么：注册→登录→搜索→加好友→发消息→图片→语音→改资料→索要资料→已读；改密、删好友、注销
     接口：searchfriend/addfriend/newfriends/askforfriendinfor/changeinformation/findpassword1~3/deletefriend 等
     用途：验证主流程端到端是否正常

  2. batch - 批量心跳
     测什么：多连接批量发送心跳包
     接口：heart
     用途：验证心跳并发、连接稳定性

  3. stress - 压测
     测什么：大量连接持续发心跳，观察连接数/进度
     接口：heart
     用途：压力测试、连接上限

  4. reg - 批量注册
     测什么：批量创建账号
     接口：register
     用途：预创建账号、注册接口压测

  5. mixed - 混合压测
     测什么：多 worker 并发执行 reg/login/heart/msg/friendinfo/messageread/changeinfo/findpwd
     接口：多种
     用途：混合场景压测

  6. msg - 批量消息
     测什么：用户对之间发文本消息
     接口：addfriend/newfriends + 消息发送
     用途：消息接口压测

  7. friend - 批量好友
     测什么：N 人加 1 中心用户为好友
     接口：searchfriend/addfriend/newfriends
     用途：加好友接口压测（查找用户→发送请求→接受）

  8. findpwd - 找回密码
     测什么：批量执行找回密码流程
     接口：findpassword1/2/3
     用途：找回密码接口压测

  9. changeinfo - 修改资料
     测什么：批量修改昵称、头像等
     接口：changeinformation
     用途：修改资料接口压测

  10. deletefriend - 删除好友
      测什么：用户对之间删除好友关系
      接口：addfriend/newfriends + deletefriend
      用途：删除好友接口压测

  11. friendinfo - 索要好友信息  ★ 注意：不是「查找用户+添加好友」
      测什么：已是好友的双方，一方向服务器请求对方的详细资料（昵称、头像、签名等）
      接口：askforfriendinfor（请求）→ friendinfor（响应）
      用途：好友资料拉取接口压测
      说明：查找用户是 searchfriend，添加好友是 addfriend+newfriends；索要好友信息是
            双方已是好友后，查看好友详情的接口，与加好友流程无关。

  12. document - 文件上传下载（协议类型 document）
      测什么：用户对之间上传小文件、对方下载
      接口：addfriend/newfriends + 文件上传/下载（messagetype=document）
      用途：文件传输接口压测（小文件场景）

  13. clouddrive - 速聊网盘
      测什么：上传获 file_id、listmycloudfiles、searchcloudfile、他人凭 ID 下载
      接口：upload_begin(cloud=true) + cloud_upload_done；searchcloudfile/listmycloudfiles；
             askforcloudfile + document_begin/end（无需好友关系）
      用途：网盘存储端到端与公开分享下载验证

  14. messageread - 消息已读
      测什么：发消息后对方标记已读
      接口：addfriend/newfriends + 消息发送 + uploadSucceed(已读)
      用途：已读状态接口压测

  【协议补充·未单独开测试模式】
    - 登录链 `askforloginmes2` 的 JSON 应答 tag 为 `loginmessage2`，可含字段 `session_token`（每 TCP 连接、每次成功绑定账号后更新）。
    - 仅调用 `chat_ai_analyze` 时须在 JSON 中携带与当前连接一致的 `session_token`；其它 tag（如 heart、messages）不要求 token。
    - 帧头第 1 字节为 `PROTOCOL_WIRE_VERSION`（与 `depend/SocketDepend.h`、`PROTOCOL_WIRE_VERSION` 常量一致，当前为 0xC5）。

================================================================================
【单独测试某一部分】使用 --mode 指定模式，默认参数见下方各配置区
================================================================================

  python test_client.py --mode full          # 完整流程（5步）
  python test_client.py --mode batch         # 批量心跳
  python test_client.py --mode stress        # 压测
  python test_client.py --mode reg           # 批量注册
  python test_client.py --mode mixed         # 混合压测
  python test_client.py --mode msg           # 批量消息
  python test_client.py --mode friend        # 批量好友
  python test_client.py --mode findpwd       # 找回密码
  python test_client.py --mode changeinfo    # 修改资料
  python test_client.py --mode deletefriend  # 删除好友
  python test_client.py --mode friendinfo    # 索要好友信息（askforfriendinfor）
  python test_client.py --mode document      # 文件上传下载
  python test_client.py --mode clouddrive    # 速聊网盘
  python test_client.py --mode messageread   # 消息已读

【覆盖默认参数】可用 --count、--conn、--duration、--upload-size、--full-pairs 等（并发类仍见 CONFIG 各字典）：

  python test_client.py --mode reg --count 50          # 只注册 50 个
  python test_client.py --mode stress --conn 200 --duration 30
  python test_client.py --mode full --full-pairs 3
  python test_client.py --mode document --count 4      # 只测 4 对文件

【全部测试】--mode all（默认），会依次执行 14 项，报告写入 test_report_*.txt

  python test_client.py
  python test_client.py -o report.txt

可改默认规模/超时：见本文件「CONFIG」一节（HOST、各字典）。CLI 覆盖关系见 docs/测试脚本说明.md。
"""

import socket
import struct
import json
import time
import sys
import os
import uuid
import argparse
import threading
import random
import base64
import builtins


_PRINT_LOCK = threading.Lock()


def _safe_print(*args, **kwargs):
    """线程安全打印，避免并发进度输出粘连到同一行。"""
    with _PRINT_LOCK:
        builtins.print(*args, **kwargs)


print = _safe_print

# ----- 协议常量（须与服务端一致；一般不要改）-----
PROTOCOL_HEADER_LEN = 8  # 每条消息：8 字节头 + 正文
PAYLOAD_JSON = 0  # 负载类型：JSON 文本
PAYLOAD_BINARY = 1  # 负载类型：二进制（图片、文件分片等）


def normalize_account_key(acc):
    """
    将账号统一为与库中 account_number 一致的字符串，供 login / logout 等 JSON 字段使用。
    避免 int / str / float（JSON）、BOM、首尾空白、NBSP、零宽字符等与库中主键不一致。
    """
    if acc is None:
        return ""
    if isinstance(acc, bool):
        return str(acc)
    if isinstance(acc, int):
        return str(acc)
    if isinstance(acc, float):
        if acc != acc:  # NaN
            return ""
        try:
            as_int = int(acc)
            if abs(acc - float(as_int)) < 1e-9:
                return str(as_int)
        except (ValueError, OverflowError):
            pass
        s = ("%s" % acc).strip()
    elif isinstance(acc, bytes):
        try:
            s = acc.decode("utf-8", errors="replace").strip()
        except Exception:
            return ""
    else:
        s = str(acc).strip()
    s = s.lstrip("\ufeff").strip()
    s = s.replace("\u00a0", "").replace("\u200b", "").replace("\u200c", "").replace("\u200d", "").strip()
    # 纯数字账号与服务端 BIGINT/字符串混存时，可能出现前导零等，与库主键不一致；统一成无符号十进制
    if s.isdigit():
        try:
            s = str(int(s))
        except ValueError:
            pass
    return s


# ############################
# # CONFIG：可改默认测试参数  #
# ############################
# 约定：时间未写单位则为「秒」；并发写 -1 表示由脚本按「全并发」处理。
# HOST/PORT 也是命令行 --host/--port 的默认值；命令行与字典关系见 docs/测试脚本说明.md。
# 说明：Qt 客户端主连接心跳等间隔在 ClientConfigDefaults 代码常量中（不写 Settings.ini），与本脚本无关。

# ----- 连接谁 -----
HOST = "47.104.181.27"  # 服务器地址（IP 或域名）
#HOST = "127.0.0.1"  # 服务器地址（IP 或域名）
PORT = 20001  # TCP 端口（与服务器 Settings.ini 里 ListenPort 一致）

# ----- 超时（秒）：只改这里即可调全局；下面从「快」到「慢」 -----
RECV_TIMEOUT_INSTANT = 15.0  # 收包极短：例如顺带读掉一帧、document 里小步等待
RECV_TIMEOUT_LOGIN = 20.0  # 登录/注册链路上单步收包、中等长度交互
RECV_TIMEOUT_STANDARD = 25.0  # 常用档：一般业务、加好友子步骤、等 heart 就绪
RECV_TIMEOUT_RELAXED = 30.0  # 稍慢：例如中心账号收包、略慢的接口
RECV_TIMEOUT_SOCIAL = 40.0  # 更慢：addfriend / newfriends 等协商链
RECV_TIMEOUT_HEAVY = 45.0  # 最慢档：数据库可能排队、大步骤、文档泵送前等待
RECV_TIMEOUT_SHORT = RECV_TIMEOUT_STANDARD  # 别名，等于上一行，老代码里叫 SHORT
RECV_TIMEOUT_DEFAULT = RECV_TIMEOUT_HEAVY  # 别名；recv_packet 不写 timeout= 时用此值
# 注销：登录后可能先收到推送，再等 logout 回包；服务端 DB 线程池忙时排队较久，单独放宽
RECV_TIMEOUT_LOGOUT = 35.0
CONNECT_TIMEOUT = 30.0  # 仅建立 TCP 连接（socket.connect）的最长等待
SEND_TIMEOUT_DEFAULT = 120.0  # 单次把数据写完套接字的最长等待（大 JSON/分片防卡死）
THREAD_JOIN_TIMEOUT_SEC = RECV_TIMEOUT_STANDARD  # 子线程 Thread.join 最多等多久（如混合压测收尾）

# ----- 等上传结束（uploaddone）-----
UPLOAD_DONE_TIMEOUT = 60.0  # 单次 recv 等「上传完成」类回复的最长时间（秒）
UPLOAD_DONE_MAX_LOOPS = 150  # 上面单次超时内，最多轮询 recv 多少次

# ----- FULL：命令行 --mode full，五步全流程 -----
FULL = {
    "pairs": 5,  # 第 1 步：主流程「发方+收方」对数；可用 --full-pairs 覆盖
    "document_pairs": 6,  # 第 2 步：文档上传/下载用多少对账号
    "upload_pairs": 6,  # 第 3 步：大文件上传用多少对账号
    "msg_per_pair": 100,  # 每对账号发多少条文本消息
    "msg_interval_sec": 0.06,  # 两条消息之间隔多久（秒），减轻服务端背压、少断连
    "upload_size_mb": 10,  # 大文件测试时单路最多读多少 MB（可用 --upload-size 覆盖；须传给 get_upload_content）
    "document_concurrent_max": 999,  # 文档子任务并发上限（防发送队列堆满）
    "upload_chunk_interval_sec": 0.1,  # 大文件每个分片之间隔多久（秒）
    "progress_every": 1,  # 第 1 步每完成几对打印一次进度
    "concurrent_step1": 5,  # 第 1 步最多几对同时跑
    "concurrent_step2": -1,  # 第 2 步并发；-1 表示不限制
    "concurrent_step3": -1,  # 第 3 步大文件并发上限（略降默认，减轻线程池/内存尖峰）
    "concurrent_step4": -1,  # 第 4 步（改密删友）；-1 不限制
    "concurrent_step5": 5,  # 第 5 步（注销）同时跑几条；-1 表示不限制
}

# ----- BATCH：--mode batch，批量连上去发心跳 -----
BATCH = {
    "count": 500,  # 一共发多少次 heart；可用 --count 覆盖
    "batch": 100,  # 每一批开多少条连接
    "progress_every": 100,  # 每完成多少次打印进度
}

# ----- STRESS：--mode stress，多连接持续压测 -----
STRESS = {
    "conn": 300,  # 同时保持多少条 TCP（默认略低于常见线程池上限，避免 full 后立刻打满）；可用 --conn 覆盖
    "duration": 50,  # 压测持续多少秒；可用 --duration 覆盖
    "progress_every": 60,  # 每多少轮打印一次进度
}

# ----- REG：--mode reg，批量注册账号 -----
REG = {
    "count": 100,  # 注册多少个号；可用 --count 覆盖；mode=all 时后面测试会复用这些号
    "batch": 5,  # 每批同时注册几个
    "progress_every": 20,  # 每注册几个打印进度
    "recv_timeout": RECV_TIMEOUT_DEFAULT,  # 注册流程里每次 recv 的最长等待（一般用默认慢档即可）
    "retries": 5,  # 失败时最多重试几次
    "retry_sleep": 0.5,  # 重试前睡眠基数（秒），实际会递增
    "batch_interval": 0.2,  # 每批做完后暂停多久（秒），给服务端喘息
}

# ----- MIXED：--mode mixed，多线程混合操作压测 -----
MIXED = {
    "duration": 50,  # 整段压测跑多少秒；可用 --duration 覆盖
    "pairs": 50,  # 预先准备多少对「用户 A+用户 B」
    "msg_per_loop": 4,  # 每对每一轮发几条消息
    "recv_timeout": RECV_TIMEOUT_DEFAULT,  # 各 worker 里 recv 的最长等待
    "workers": {  # 每种逻辑开几个线程；0 表示不跑这类操作
        "reg": 2,  # 线程里跑注册
        "login": 3,  # 登录
        "heart": 4,  # 心跳
        "msg": 4,  # 发消息
        "doc": 2,  # 文档（0=关闭）
        "upload": 2,  # 大文件上传（0=关闭）
        "friendinfo": 3,  # 要好友资料
        "messageread": 3,  # 已读
        "changeinfo": 3,  # 改资料
        "findpwd": 3,  # 找回密码
    },
    "progress_every": 10,  # 加好友阶段每几对打印进度
    "addfriend_concurrent": 5,  # 准备阶段「成对加好友」并行对数（与 ADDFRIEND_PAIR_CONCURRENT 一致即可）
}

# ----- MSG：--mode msg，成对发消息 -----
MSG = {
    "pairs": 50,  # 多少对用户；--count 覆盖的是「对数」
    "msg_per_pair": 5,  # 每对发几条
    "concurrent": -1,  # 并发；-1=全并发
    "progress_every": 20,  # 加好友阶段每几对打印进度
}

# ----- FRIEND：--mode friend，多人加一个中心好友 -----
FRIEND = {
    "count": 99,  # 多少人去加同一个中心用户；--count 可覆盖
    "concurrent": 5,  # 复用 reg 时，与中心加好友的并行人数
    "progress_every": 20,  # 每几个人打印进度
}

# ----- FINDPWD：--mode findpwd -----
FINDPWD = {
    "count": 100,  # 跑多少次「找回密码」完整流程
    "concurrent": -1,  # -1=全并发
    "progress_every": 20,
}

# ----- CHANGEINFO：--mode changeinfo -----
CHANGEINFO = {
    "count": 60,  # 改资料多少次
    "concurrent": 5,  # 每批几个账号一起跑
    "progress_every": 5,
}

# ----- DELETEFRIEND：--mode deletefriend -----
DELETEFRIEND = {
    "pairs": 50,  # 多少对好友执行删除；--count 表示对数
    "concurrent": -1,  # -1=全并发
    "progress_every": 50,  # 加好友阶段每几对打印进度
}

# ----- FRIENDINFO：--mode friendinfo，要好友详细资料 -----
FRIENDINFO = {
    "pairs": 20,  # 多少对
    "req_per_pair": 1,  # 每对请求几次好友信息
    "concurrent": 5,  # 每批同时跑几对
    "progress_every": 5,
}

# ----- DOCUMENT：--mode document（文件消息，messagetype=document）-----
DOCUMENT = {
    "pairs": 8,  # 文件上/下载用几对账号
    "concurrent": 4,  # 文件任务并发数
    "progress_every": 10,
    "chunk_interval_sec": 0.03,  # 分片间隔（秒），减轻背压
}

# ----- CLOUDDRIVE：--mode clouddrive（速聊网盘，upload_begin.cloud=true）-----
CLOUDDRIVE = {
    "pairs": 4,  # 几组「上传者 + 下载者」账号（无需好友）
    "concurrent": 2,  # 并发组数
    "progress_every": 5,
    "chunk_interval_sec": 0.03,  # 上传分片间隔（秒）
}

# ----- MESSAGEREAD：--mode messageread -----
MESSAGEREAD = {
    "pairs": 50,  # 几对用户做已读测试
    "read_per_pair": 1,  # 每对做几轮已读
    "concurrent": -1,  # -1=全并发
    "progress_every": 20,
}

# ----- mode=all 时的执行顺序（便于对照日志）-----
# full → batch → stress → reg（生成账号表）→ mixed → msg → friend → findpwd →
# changeinfo → deletefriend → friendinfo → document → clouddrive → messageread。
# deletefriend 会删好友，后面几个模式会重新加好友。

# 测试资源目录（相对路径：test/res）
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RES_DIR = os.path.join(_SCRIPT_DIR, "res")
RES_AVATAR = os.path.join(RES_DIR, "example2.png")
RES_IMAGE = os.path.join(RES_DIR, "example1.jpg")
RES_VIDEO = os.path.join(RES_DIR, "example3.mp4")
RES_AUDIO = os.path.join(RES_DIR, "example4")

_TINY_PNG = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="


def _load_file(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except Exception:
        return None


def _load_file_base64(path):
    data = _load_file(path)
    if data:
        return base64.b64encode(data).decode("ascii")
    return _TINY_PNG


def get_avatar_base64():
    return _load_file_base64(RES_AVATAR)


def get_document_content():
    data = _load_file(RES_IMAGE)
    if data:
        return data
    data = _load_file(RES_AVATAR)
    if data:
        return data
    return base64.b64decode(_TINY_PNG)


def get_document_filename():
    if _load_file(RES_IMAGE):
        return os.path.basename(RES_IMAGE)
    if _load_file(RES_AVATAR):
        return os.path.basename(RES_AVATAR)
    return "doc.bin"


def get_upload_content(size_limit=None):
    data = _load_file(RES_VIDEO)
    if not data:
        data = _load_file(RES_IMAGE)
    if not data:
        return None
    if size_limit and len(data) > size_limit:
        return data[:size_limit]
    return data


def get_audio_content():
    for ext in (".audio", "", ".ogg", ".mp3", ".wav", ".m4a"):
        p = RES_AUDIO + ext if ext else RES_AUDIO
        data = _load_file(p)
        if data:
            return data
    return None


def get_picture_content():
    """获取发送图片用内容（picture 消息）"""
    return _load_file(RES_IMAGE)


def login_with_fallback(sock, account, timeout=None, passwords=("123456", "654321")):
    """登录兜底：先试默认密码，再试找密后密码。成功返回 True。"""
    if timeout is None:
        timeout = RECV_TIMEOUT_DEFAULT
    for pwd in passwords:
        try:
            send_json(sock, {"tag": "login", "account_number": account, "password": pwd})
            r, _ = recv_packet(sock, timeout=timeout)
            if r and r.get("answer") == "loginsucceed":
                return True
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            return False
    return False


def add_friend_pair(host, port, acc_s, acc_r, timeout=None):
    """加好友一对：发方 addfriend，收方 newfriends accept。
    duplicate 表示服务端认为重复（如已有 pending/或已是好友等，以服务端为准）。
    返回三元组 (addfriend_ok, newfriends_ok, add_answer)：
      addfriend_ok/newfriends_ok 各为 0 或 1；
      add_answer 为 \"ok\"、\"duplicate\" 或 None（未拿到有效 addfriend 应答时）。"""
    if timeout is None:
        timeout = RECV_TIMEOUT_DEFAULT
    s, _ = connect(host, port)
    if not s:
        return 0, 0, None
    if not login_with_fallback(s, acc_s, timeout=timeout):
        s.close()
        return 0, 0, None
    send_json(s, {"tag": "addfriend", "account": acc_s, "friend": acc_r})
    r_add, _ = recv_packet(s, timeout=timeout)
    s.close()
    if not r_add or r_add.get("answer") not in ("ok", "duplicate"):
        return 0, 0, None
    add_answer = r_add.get("answer")
    s, _ = connect(host, port)
    if not s:
        return 1, 0, add_answer
    if not login_with_fallback(s, acc_r, timeout=timeout):
        s.close()
        return 1, 0, add_answer
    send_json(s, {"tag": "newfriends", "account": acc_r, "sender": acc_s, "answer": "accept"})
    r_new, _ = recv_packet(s, timeout=timeout)
    s.close()
    if r_new and r_new.get("tag") == "updatefriendship":
        return 1, 1, add_answer
    # addfriend 成功但未见 updatefriendship：可能 accept 失败或响应丢失
    return ((1, 0, add_answer) if add_answer in ("ok", "duplicate") else (0, 0, None))


# 成对加好友阶段（add_friend_pair）默认并行对数；与 FRIEND["concurrent"] 一致
ADDFRIEND_PAIR_CONCURRENT = 5


def add_friend_pairs_parallel(host, port, pairs, concurrent, progress_every, log_prefix):
    """pairs 为 [(acc_s, acc_r), ...]。分批起线程调用 add_friend_pair，返回 (addfriend_ok 累计, newfriends_ok 累计)。"""
    if not pairs:
        return 0, 0
    concurrent = max(1, min(int(concurrent), len(pairs)))
    addfriend_ok = newfriends_ok = 0
    state_lock = threading.Lock()
    done = [0]
    n = len(pairs)

    def work(acc_s, acc_r):
        nonlocal addfriend_ok, newfriends_ok
        a, nf, _ = add_friend_pair(host, port, acc_s, acc_r)
        with state_lock:
            addfriend_ok += a
            newfriends_ok += nf
            done[0] += 1
            d = done[0]
            if progress_every and (d % progress_every == 0 or d == 1 or d == n):
                okp = min(addfriend_ok, newfriends_ok)
                print("    [进度] %s: %d/%d, 成功 %d 对 失败 %d 对 [%s]" % (
                    log_prefix, d, n, okp, d - okp, _ts()), flush=True)

    for start in range(0, n, concurrent):
        threads = []
        for idx in range(start, min(start + concurrent, n)):
            acc_s, acc_r = pairs[idx]
            threads.append(threading.Thread(target=work, args=(acc_s, acc_r)))
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    return addfriend_ok, newfriends_ok


# 预加载头像（注册/修改资料用），避免重复读文件
AVATAR_BASE64 = get_avatar_base64()


# 须与 user/depend/SocketDepend.h、server/depend/SocketDepend.h 中 PROTOCOL_WIRE_VERSION 一致
PROTOCOL_WIRE_VERSION = 0xC5


def make_header(data_len, payload_type=PAYLOAD_JSON):
    """构造 8 字节协议头：版本、负载类型、正文长度（大端 u32）。"""
    return struct.pack(">B3sI", PROTOCOL_WIRE_VERSION, bytes([payload_type, 0, 0]), data_len)


def send_json(sock, obj, timeout=None):
    """发送一条 JSON 业务包（带头部的 PAYLOAD_JSON）。"""
    if sock is None:
        raise ConnectionError("socket is None")
    if timeout is None:
        timeout = SEND_TIMEOUT_DEFAULT
    body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    payload = make_header(len(body), PAYLOAD_JSON) + body
    sock.settimeout(timeout)
    try:
        sock.sendall(payload)
    finally:
        sock.settimeout(None)


def send_binary(sock, data, timeout=None):
    """发送一条二进制负载包（带头部的 PAYLOAD_BINARY）。"""
    if timeout is None:
        timeout = SEND_TIMEOUT_DEFAULT
    payload = make_header(len(data), PAYLOAD_BINARY) + data
    sock.settimeout(timeout)
    try:
        sock.sendall(payload)
    finally:
        sock.settimeout(None)


def send_doc_chunk(sock, file_uuid, seq, chunk):
    """发送文档二进制分片，格式与 Qt SocketDoc 一致: [frameType=1][uuid 16B][seq 4B BE][chunkLen 4B BE][chunk]"""
    body = bytes([1]) + uuid.UUID(file_uuid).bytes
    body += struct.pack(">I", seq)
    body += struct.pack(">I", len(chunk))
    body += chunk
    send_binary(sock, body)


def recv_packet(sock, timeout=None):
    """接收一条完整帧；成功返回 (json 或 bytes, 负载类型)，失败返回 (None, None)。"""
    if timeout is None:
        timeout = RECV_TIMEOUT_DEFAULT
    sock.settimeout(timeout)
    try:
        header = sock.recv(PROTOCOL_HEADER_LEN)
        if len(header) < PROTOCOL_HEADER_LEN:
            return None, None
        _, reserved, data_len = struct.unpack(">B3sI", header)
        payload_type = reserved[0] if reserved else PAYLOAD_JSON
        if data_len > 16 * 1024 * 1024:
            return None, None
        body = b""
        while len(body) < data_len:
            chunk = sock.recv(min(65536, data_len - len(body)))
            if not chunk:
                return None, None
            body += chunk
        if payload_type == PAYLOAD_BINARY:
            return body, PAYLOAD_BINARY
        return json.loads(body.decode("utf-8")), PAYLOAD_JSON
    except Exception:
        return None, None
    finally:
        sock.settimeout(None)


def connect(host, port):
    """建立 TCP 连接；成功返回 (socket, 连接耗时 ms)，失败返回 (None, 0)。"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(CONNECT_TIMEOUT)
        t0 = time.perf_counter()
        s.connect((host, port))
        elapsed = (time.perf_counter() - t0) * 1000
        s.settimeout(None)
        return s, elapsed
    except Exception:
        return None, 0


def wait_for_server_ready(host, port, max_retries=10, retry_delay=1.0):
    """等待服务端就绪（连接+心跳成功），避免 full 第一步在服务端未初始化完成时失败"""
    print("    [等待] 服务端就绪检查 %s:%d ..." % (host, port), flush=True)
    for i in range(max_retries):
        s, _ = connect(host, port)
        if not s:
            if i < max_retries - 1:
                print("    [等待] 第 %d/%d 次连接失败，%s 秒后重试" % (i + 1, max_retries, retry_delay), flush=True)
            time.sleep(retry_delay)
            continue
        try:
            send_json(s, {"tag": "heart"})
            r, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            s.close()
            if r and r.get("tag") == "pong":
                if i > 0:
                    print("    [就绪] 服务端已就绪 (重试 %d 次后)" % i, flush=True)
                else:
                    print("    [就绪] 服务端已就绪", flush=True)
                return True
        except Exception:
            pass
        if i < max_retries - 1:
            print("    [等待] 心跳未响应，%s 秒后重试" % retry_delay, flush=True)
        time.sleep(retry_delay)
    return False


def ok(msg, ms=None):
    if ms is not None:
        print("    [通过] %s  (耗时 %.1f ms)" % (msg, ms))
    else:
        print("    [通过] %s" % msg)


def fail(msg):
    print("    [失败] %s" % msg)


def _ts():
    """返回当前时间戳用于进度输出"""
    return time.strftime("%H:%M:%S")


def conc_paren(cfg_val, resolved_unbounded=None):
    """用户可见的统一并发标注：(并发：N)。配置为 -1 时用 resolved_unbounded（如实际对数/线程数）。"""
    if cfg_val is None:
        return "(并发：默认)"
    try:
        iv = int(cfg_val)
    except (TypeError, ValueError):
        return "(并发：%s)" % cfg_val
    if iv == -1:
        if resolved_unbounded is not None:
            return "(并发：%d)" % int(resolved_unbounded)
        return "(并发：全)"
    return "(并发：%d)" % iv


def section(title):
    print()
    print("=" * 50)
    print("  %s  [%s]" % (title, _ts()))
    print("=" * 50)


def _print_execution_plan(args):
    """本次执行将要进行的所有测试，一次性输出"""
    host, port = args.host, args.port
    fp = getattr(args, "full_pairs", None) or FULL.get("pairs", 5)
    print()
    print("=" * 50)
    print("  本次执行测试  %s:%d" % (host, port))
    print("  测试开始时间: %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
    print("=" * 50)
    if args.mode == "all":
        print()
        print("  将依次执行以下测试：")
        print()
        _fp = getattr(args, "full_pairs", None) or FULL.get("pairs", 5)
        _fp = max(1, _fp)
        _c1 = FULL.get("concurrent_step1", 3)
        _c1e = _fp if _c1 == -1 else max(1, min(int(_c1), _fp))
        _d = FULL.get("document_pairs", 6)
        _c2 = FULL.get("concurrent_step2", -1)
        _c2e = min(_d if _c2 == -1 else _c2, FULL.get("document_concurrent_max", 4))
        _up = FULL.get("upload_pairs", 6)
        _c3 = FULL.get("concurrent_step3", 2)
        _c3e = _up if _c3 == -1 else _c3
        _c4 = FULL.get("concurrent_step4", -1)
        _c4e = _fp if _c4 == -1 else max(1, min(int(_c4), _fp))
        _c5 = FULL.get("concurrent_step5", -1)
        _logout_n = 2 * _fp + 2 * _d + 2 * _up
        _c5e = _logout_n if _c5 == -1 else max(1, min(int(_c5), _logout_n))
        print("  1. full - 用户完整操作流程 一(并发：%d) 二(并发：%d) 三(并发：%d) 四(并发：%d) 五(并发：%d)"
              % (_c1e, _c2e, _c3e, _c4e, _c5e))
        print()
        print("  2. batch - 批量心跳 %d 次 (并发：%d)" % (BATCH.get("count", 500), BATCH.get("batch", 100)))
        print()
        print("  3. stress - 压测 %d 秒 (并发：%d)" % (STRESS.get("duration", 50), STRESS.get("conn", 300)))
        print()
        reg_n = REG.get("count", 150)
        print("  4. reg - 批量注册 %d 个 (并发：%d)" % (reg_n, REG.get("batch", 8)))
        print()
        _mw = MIXED.get("workers", {})
        _mks = ("reg", "login", "heart", "msg", "doc", "upload", "friendinfo", "messageread", "changeinfo", "findpwd")
        _ms = sum(int(_mw.get(k, 0)) for k in _mks)
        print("  5. mixed - 混合压测 %d 秒（worker 线程合计 %d）（复用）" % (MIXED.get("duration", 50), _ms))
        print()
        _mp = MSG.get("pairs", 75)
        print("  6. msg - 批量消息 %d 对 %s（复用）" % (_mp, conc_paren(MSG.get("concurrent", -1), _mp)))
        print()
        print("  7. friend - 批量好友 %d 人加 1 人 %s（复用）" % (FRIEND.get("count", 149), conc_paren(FRIEND.get("concurrent", ADDFRIEND_PAIR_CONCURRENT))))
        print()
        _fc = FINDPWD.get("count", 75)
        print("  8. findpwd - 批量找回密码 %d 次 %s（复用）" % (_fc, conc_paren(FINDPWD.get("concurrent", -1), _fc)))
        print()
        print("  9. changeinfo - 批量修改资料 %d 次 (并发：%d)（复用）" % (CHANGEINFO.get("count", 75), CHANGEINFO.get("concurrent", 8)))
        print()
        _dp = DELETEFRIEND.get("pairs", 75)
        print("  10. deletefriend - 批量删除好友 %d 对 %s（复用）" % (_dp, conc_paren(DELETEFRIEND.get("concurrent", -1), _dp)))
        print()
        print("  11. friendinfo - 批量索要好友信息 %d 对 (并发：%d)（复用）" % (FRIENDINFO.get("pairs", 75), FRIENDINFO.get("concurrent", 8)))
        print()
        print("  12. document - 批量文件 %d 对 (并发：%d)（复用）" % (DOCUMENT.get("pairs", 8), DOCUMENT.get("concurrent", 8)))
        print()
        print("  13. clouddrive - 速聊网盘 %d 组 (并发：%d)（复用）" % (CLOUDDRIVE.get("pairs", 4), CLOUDDRIVE.get("concurrent", 2)))
        print()
        _mrp = MESSAGEREAD.get("pairs", 75)
        print("  14. messageread - 批量消息已读 %d 对 %s（复用）" % (_mrp, conc_paren(MESSAGEREAD.get("concurrent", -1), _mrp)))
    else:
        mode_cfg = {"batch": BATCH, "stress": STRESS, "reg": REG, "mixed": MIXED, "msg": MSG, "friend": FRIEND,
                    "findpwd": FINDPWD, "changeinfo": CHANGEINFO, "deletefriend": DELETEFRIEND,
                    "friendinfo": FRIENDINFO, "document": DOCUMENT, "clouddrive": CLOUDDRIVE,
                    "messageread": MESSAGEREAD}
        cfg = mode_cfg.get(args.mode, {})
        count = args.count if args.count is not None else cfg.get("count", cfg.get("pairs", 10))
        print()
        print("  将执行：%s" % args.mode)
        if args.mode == "full":
            _fp = max(1, fp)
            _c1 = FULL.get("concurrent_step1", 3)
            _c1e = _fp if _c1 == -1 else max(1, min(int(_c1), _fp))
            _d = FULL.get("document_pairs", 6)
            _c2 = FULL.get("concurrent_step2", -1)
            _c2e = min(_d if _c2 == -1 else _c2, FULL.get("document_concurrent_max", 4))
            _up = FULL.get("upload_pairs", 6)
            _c3 = FULL.get("concurrent_step3", 2)
            _c3e = _up if _c3 == -1 else _c3
            _c4 = FULL.get("concurrent_step4", -1)
            _c4e = _fp if _c4 == -1 else max(1, min(int(_c4), _fp))
            _c5 = FULL.get("concurrent_step5", -1)
            _logout_n = 2 * _fp + 2 * _d + 2 * _up
            _c5e = _logout_n if _c5 == -1 else max(1, min(int(_c5), _logout_n))
            print("    - 主流程 %d 对；一(并发：%d) 二(并发：%d) 三(并发：%d) 四(并发：%d) 五(并发：%d)"
                  % (_fp, _c1e, _c2e, _c3e, _c4e, _c5e))
            print("    - 注册→登录→加好友→发消息→图片→语音→改资料→索要资料→已读")
            print("    - 改密、删好友、注销")
        elif args.mode == "batch":
            print("    - %d 次心跳 (并发：%d)" % (count, BATCH.get("batch", 100)))
        elif args.mode == "stress":
            _sc = args.conn if args.conn is not None else STRESS.get("conn", 300)
            _sd = args.duration if args.duration is not None else STRESS.get("duration", 50)
            print("    - %d 秒 (并发：%d)" % (_sd, _sc))
        elif args.mode == "mixed":
            _md = args.duration if args.duration is not None else MIXED.get("duration", 50)
            _mw = MIXED.get("workers", {})
            _mks = ("reg", "login", "heart", "msg", "doc", "upload", "friendinfo", "messageread", "changeinfo", "findpwd")
            _ms = sum(int(_mw.get(k, 0)) for k in _mks)
            print("    - 混合压测 %d 秒（worker 合计 %d）" % (_md, _ms))
        elif args.mode == "reg":
            print("    - 注册 %d 个 (并发：%d)" % (count, REG.get("batch", 8)))
        elif args.mode == "msg":
            print("    - %d 对发消息 %s" % (count, conc_paren(MSG.get("concurrent", -1), count)))
        elif args.mode == "friend":
            print("    - %d 人加 1 人 %s" % (count, conc_paren(FRIEND.get("concurrent", ADDFRIEND_PAIR_CONCURRENT))))
        elif args.mode == "findpwd":
            print("    - 找回密码 %d 次 %s" % (count, conc_paren(FINDPWD.get("concurrent", -1), count)))
        elif args.mode == "changeinfo":
            print("    - 修改资料 %d 次 (并发：%d)" % (count, CHANGEINFO.get("concurrent", 8)))
        elif args.mode == "deletefriend":
            print("    - 删除好友 %d 对 %s" % (count, conc_paren(DELETEFRIEND.get("concurrent", -1), count)))
        elif args.mode == "friendinfo":
            print("    - 索要好友信息 %d 对 (并发：%d)" % (count, FRIENDINFO.get("concurrent", 8)))
        elif args.mode == "document":
            print("    - %d 对文件上传下载 (并发：%d)" % (count, DOCUMENT.get("concurrent", 8)))
        elif args.mode == "clouddrive":
            print("    - %d 组网盘上传/查询/下载 (并发：%d)" % (count, CLOUDDRIVE.get("concurrent", 2)))
        else:
            print("    - 消息已读 %d 对 %s" % (count, conc_paren(MESSAGEREAD.get("concurrent", -1), count)))
    print()


def run_full_test(host, port, upload_size_mb=None, num_messages=None, concurrent_uploads=None, full_pairs=None):
    cfg = FULL
    num_msg = num_messages if num_messages is not None else cfg.get("msg_per_pair", 150)
    conc_uploads = concurrent_uploads if concurrent_uploads is not None else cfg.get("upload_pairs", 4)
    size_mb = upload_size_mb if upload_size_mb is not None else cfg.get("upload_size_mb", 2)
    n_pairs = full_pairs if full_pairs is not None else cfg.get("pairs", 5)
    n_pairs = max(1, n_pairs)
    conc_step1 = cfg.get("concurrent_step1", -1)
    if conc_step1 == -1:
        conc_step1 = n_pairs
    conc_step1 = max(1, min(int(conc_step1), n_pairs))
    suffix = str(int(time.time() * 1000))[-6:]
    total_start = time.perf_counter()

    # 等待服务端就绪，避免第一步注册时服务端尚未初始化完成
    if not wait_for_server_ready(host, port):
        print("    [失败] 服务端未就绪，跳过 full 测试")
        return

    main_pairs = []  # [(acc_sender, acc_receiver), ...]
    pair_lock = threading.Lock()
    pair_errors = []
    pair_done = [0]  # 已完成对数（含成功/失败）
    pair_progress = {}  # pair_idx -> 当前步骤描述，用于细粒度进度

    def _set_progress(idx, msg):
        with pair_lock:
            pair_progress[idx] = msg

    def _run_one_pair_steps_1_to_10(pair_idx):
        acc_sender = acc_receiver = None
        try:
            _set_progress(pair_idx, "发方注册")
            resp = None
            for attempt in range(5):
                sock_sender, _ = connect(host, port)
                if not sock_sender:
                    time.sleep(0.2 * (attempt + 1))
                    continue
                send_json(sock_sender, {"tag": "register", "nickname": "测试发%d_%s" % (pair_idx + 1, suffix),
                          "gender": "男", "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
                resp, _ = recv_packet(sock_sender, timeout=RECV_TIMEOUT_DEFAULT)
                sock_sender.close()
                if resp and resp.get("answer") == "regissucceed":
                    acc_sender = resp.get("account_number", "")
                    break
                time.sleep(0.5 * (attempt + 1))
            if not acc_sender:
                diag = "超时/无响应" if not resp else ("answer=%s" % resp.get("answer", resp))
                with pair_lock:
                    pair_errors.append("pair%d: 发方注册失败 (%s)" % (pair_idx + 1, diag))
                return None

            _set_progress(pair_idx, "收方注册")
            resp = None
            for attempt in range(5):
                sock_receiver, _ = connect(host, port)
                if not sock_receiver:
                    time.sleep(0.2 * (attempt + 1))
                    continue
                send_json(sock_receiver, {"tag": "register", "nickname": "测试收%d_%s" % (pair_idx + 1, suffix),
                         "gender": "女", "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
                resp, _ = recv_packet(sock_receiver, timeout=RECV_TIMEOUT_DEFAULT)
                sock_receiver.close()
                if resp and resp.get("answer") == "regissucceed":
                    acc_receiver = resp.get("account_number", "")
                    break
                time.sleep(0.5 * (attempt + 1))
            if not acc_receiver:
                diag = "超时/无响应" if not resp else ("answer=%s" % resp.get("answer", resp))
                with pair_lock:
                    pair_errors.append("pair%d: 收方注册失败 (%s)" % (pair_idx + 1, diag))
                return None

            _set_progress(pair_idx, "发方登录")
            sock_sender, _ = connect(host, port)
            send_json(sock_sender, {"tag": "login", "account_number": acc_sender, "password": "123456"})
            resp, _ = recv_packet(sock_sender, timeout=RECV_TIMEOUT_SHORT)
            if not resp or resp.get("answer") != "loginsucceed":
                with pair_lock:
                    pair_errors.append("pair%d: 发方登录失败" % (pair_idx + 1))
                sock_sender.close()
                return None

            # 登录初始化：askforloginmes0/1/2（仅第一对测试，覆盖未覆盖接口）
            if pair_idx == 0:
                send_json(sock_sender, {"tag": "askforloginmes0", "account": acc_sender})
                recv_packet(sock_sender, timeout=RECV_TIMEOUT_LOGIN)
                send_json(sock_sender, {"tag": "askforloginmes1", "account": acc_sender, "needdownload": []})
                recv_packet(sock_sender, timeout=RECV_TIMEOUT_LOGIN)
                send_json(sock_sender, {"tag": "askforloginmes2", "account": acc_sender})
                recv_packet(sock_sender, timeout=RECV_TIMEOUT_LOGIN)

            _set_progress(pair_idx, "搜索")
            search_ok = False
            for attempt in range(3):
                send_json(sock_sender, {"tag": "searchfriend", "account": acc_receiver})
                resp, _ = recv_packet(sock_sender, timeout=RECV_TIMEOUT_SOCIAL)
                if resp and resp.get("answer") == "succeed":
                    search_ok = True
                    break
                if attempt < 2:
                    time.sleep(2.0)
            if not search_ok:
                with pair_lock:
                    pair_errors.append("pair%d: 搜索失败" % (pair_idx + 1))
                sock_sender.close()
                return None

            _set_progress(pair_idx, "加好友")
            send_json(sock_sender, {"tag": "addfriend", "account": acc_sender, "friend": acc_receiver})
            addfriend_resp, _ = recv_packet(sock_sender, timeout=RECV_TIMEOUT_SOCIAL)
            if addfriend_resp and addfriend_resp.get("answer") not in ("ok", "duplicate"):
                with pair_lock:
                    pair_errors.append("pair%d: 添加好友失败" % (pair_idx + 1))
                sock_sender.close()
                return None

            _set_progress(pair_idx, "收方登录")
            sock_receiver, _ = connect(host, port)
            send_json(sock_receiver, {"tag": "login", "account_number": acc_receiver, "password": "123456"})
            resp, _ = recv_packet(sock_receiver, timeout=RECV_TIMEOUT_SHORT)
            if not resp or resp.get("answer") != "loginsucceed":
                with pair_lock:
                    pair_errors.append("pair%d: 收方登录失败" % (pair_idx + 1))
                sock_sender.close()
                sock_receiver.close()
                return None
            _set_progress(pair_idx, "接受好友")
            # duplicate = 已有 pending 申请，收方也必须 newfriends 才会写入 Friends
            send_json(sock_receiver, {"tag": "newfriends", "account": acc_receiver, "sender": acc_sender, "answer": "accept"})
            resp, _ = recv_packet(sock_receiver, timeout=RECV_TIMEOUT_SOCIAL)
            if not resp or resp.get("tag") != "updatefriendship":
                with pair_lock:
                    pair_errors.append("pair%d: 接受好友失败" % (pair_idx + 1))
                sock_sender.close()
                sock_receiver.close()
                return None

            _set_progress(pair_idx, "发消息 0/%d" % num_msg)
            msg_uuids_for_read = []
            step_interval = max(1, num_msg // 20)  # 发消息分约 20 档进度
            msg_interval = cfg.get("msg_interval_sec", 0)
            for i in range(num_msg):
                if i > 0 and i % step_interval == 0:
                    _set_progress(pair_idx, "发消息 %d/%d" % (i, num_msg))
                msg_uuid = str(uuid.uuid4())
                send_json(sock_sender, {"tag": "messages", "sender": acc_sender, "receiver": acc_receiver,
                               "messagetype": "text", "uuid": msg_uuid, "messages": "测试消息_%d" % (i + 1), "filename": ""})
                recv_packet(sock_sender, timeout=RECV_TIMEOUT_INSTANT)
                if msg_interval > 0 and i < num_msg - 1:
                    time.sleep(msg_interval)
                if len(msg_uuids_for_read) < 80:
                    msg_uuids_for_read.append(msg_uuid)

            _set_progress(pair_idx, "发图片")
            pic_content = get_picture_content()
            if pic_content:
                pic_uuid = str(uuid.uuid4())
                pic_fname = os.path.basename(RES_IMAGE)
                send_json(sock_sender, {"tag": "upload_begin", "uuid": pic_uuid, "sender": acc_sender, "receiver": acc_receiver,
                                   "filename": pic_fname, "messagetype": "picture",
                                   "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(len(pic_content))})
                resp, _ = recv_packet(sock_sender, timeout=RECV_TIMEOUT_LOGIN)
                if resp and resp.get("tag") == "upload_begin_ack":
                    chunk_size = 64 * 1024
                    chunk_interval = cfg.get("msg_interval_sec", 0)
                    for j in range(0, len(pic_content), chunk_size):
                        chunk = pic_content[j:j + chunk_size]
                        data = bytes([1]) + uuid.UUID(pic_uuid).bytes + struct.pack(">I", j // chunk_size) + struct.pack(">I", len(chunk)) + chunk
                        send_binary(sock_sender, data)
                        if chunk_interval > 0 and j + chunk_size < len(pic_content):
                            time.sleep(chunk_interval)
                    send_json(sock_sender, {"tag": "upload_end", "uuid": pic_uuid})
                    for _ in range(30):
                        resp, _ = recv_packet(sock_sender, timeout=RECV_TIMEOUT_SHORT)
                        if resp and resp.get("tag") == "uploaddone":
                            break

            _set_progress(pair_idx, "发语音")
            audio_content = get_audio_content()
            if audio_content:
                audio_b64 = base64.b64encode(audio_content).decode("ascii")
                send_json(sock_sender, {"tag": "messages", "sender": acc_sender, "receiver": acc_receiver,
                               "messagetype": "audio", "uuid": str(uuid.uuid4()), "messages": audio_b64, "filename": ""})
                recv_packet(sock_sender, timeout=RECV_TIMEOUT_LOGIN)

            _set_progress(pair_idx, "改资料")
            send_json(sock_sender, {"tag": "changeinformation", "account": acc_sender, "nickname": "测试发%d_已修改" % (pair_idx + 1),
                           "gender": "男", "signature": "新签名", "friendIds": [acc_receiver], "avator": AVATAR_BASE64, "avator_changed": True})
            recv_packet(sock_sender, timeout=RECV_TIMEOUT_RELAXED)

            _set_progress(pair_idx, "索要资料")
            send_json(sock_receiver, {"tag": "askforfriendinfor", "account": acc_receiver, "friend": acc_sender})
            recv_packet(sock_receiver, timeout=RECV_TIMEOUT_DEFAULT)

            _set_progress(pair_idx, "已读")
            if len(msg_uuids_for_read) > 0:
                send_json(sock_receiver, {"tag": "uploadSucceed", "account": acc_receiver,
                                         "unreadMessages": msg_uuids_for_read})
                recv_packet(sock_receiver, timeout=RECV_TIMEOUT_RELAXED)

            sock_sender.close()
            sock_receiver.close()
            _set_progress(pair_idx, "完成")
            return (acc_sender, acc_receiver)
        except Exception as e:
            with pair_lock:
                pair_errors.append("pair%d: %s" % (pair_idx + 1, str(e)))
            return None

    section("第一步：%d 对发方/收方：注册→登录→加好友→发消息→图片→语音→改资料→索要资料→已读 (并发：%d)" % (n_pairs, conc_step1))

    def run_pair(idx):
        result = _run_one_pair_steps_1_to_10(idx)
        with pair_lock:
            pair_done[0] += 1
            if result:
                main_pairs.append(result)

    stop_progress = threading.Event()

    def _progress_loop():
        last_snapshot = None
        while not stop_progress.wait(2):
            d = pair_done[0]
            if d >= n_pairs:
                break
            with pair_lock:
                snap = tuple((i, pair_progress.get(i, "待开始")) for i in range(n_pairs))
            if snap != last_snapshot:
                last_snapshot = snap
                ok_cnt = len(main_pairs)
                fail_cnt = len(pair_errors)
                parts = ["第%d对:%s" % (i + 1, s) for i, s in snap]
                print("    [进度] " + " | ".join(parts) + " | 成功 %d 失败 %d [%s]" % (ok_cnt, fail_cnt, _ts()), flush=True)

    progress_thread = threading.Thread(target=_progress_loop, daemon=True)
    progress_thread.start()

    idx_lock = threading.Lock()
    next_idx = [0]

    def worker():
        while True:
            with idx_lock:
                if next_idx[0] >= n_pairs:
                    return
                idx = next_idx[0]
                next_idx[0] += 1
            run_pair(idx)

    threads = []
    for _ in range(conc_step1):
        t = threading.Thread(target=worker)
        threads.append(t)
        t.start()
    for t in threads:
        t.join()
    stop_progress.set()

    if len(main_pairs) < n_pairs:
        for err in pair_errors:
            fail(err)
        fail("主流程仅 %d/%d 对成功（成功 %d 失败 %d）" % (len(main_pairs), n_pairs, len(main_pairs), len(pair_errors)))
        return False
    ok("%d 对全部完成第一步（成功 %d 失败 0）" % (n_pairs, n_pairs), None)

    doc_n = cfg.get("document_pairs", 8)
    doc_concurrent = cfg.get("concurrent_step2", 8)
    if doc_concurrent == -1:
        doc_concurrent = doc_n
    doc_concurrent = min(doc_concurrent, cfg.get("document_concurrent_max", 4))
    section("第二步：%d 对用户：每人发 1 个小文件，对应接收方下载 (并发：%d)" % (doc_n, doc_concurrent))
    file_content = get_document_content()
    small_size = len(file_content)
    doc_pairs = []
    doc_reg_ok = 0
    print("    [进度] 创建文件用户对... [%s]" % _ts(), flush=True)
    for i in range(doc_n):
        sa, _ = connect(host, port)
        sb, _ = connect(host, port)
        if not sa or not sb:
            if sa:
                sa.close()
            if sb:
                sb.close()
            if (i + 1) % 5 == 0 or i == 0:
                print("    [进度] 文件用户: 已创建 %d/%d 对, 成功 %d 失败 %d [%s]" % (i + 1, doc_n, doc_reg_ok, (i + 1) - doc_reg_ok, _ts()), flush=True)
            continue
        send_json(sa, {"tag": "register", "nickname": "文件发%d_%s" % (i + 1, suffix), "gender": "男",
                       "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
        ra, _ = recv_packet(sa, timeout=RECV_TIMEOUT_LOGIN)
        send_json(sb, {"tag": "register", "nickname": "文件收%d_%s" % (i + 1, suffix), "gender": "女",
                       "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
        rb, _ = recv_packet(sb, timeout=RECV_TIMEOUT_LOGIN)
        sa.close()
        sb.close()
        if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
            doc_pairs.append((ra.get("account_number"), rb.get("account_number")))
            doc_reg_ok += 1
        if (i + 1) % 5 == 0 or i == 0:
            print("    [进度] 文件用户: 已创建 %d/%d 对, 成功 %d 失败 %d [%s]" % (i + 1, doc_n, doc_reg_ok, (i + 1) - doc_reg_ok, _ts()), flush=True)
    print("    [进度] 文件用户创建完成: 成功 %d 对 失败 %d 对" % (doc_reg_ok, doc_n - doc_reg_ok))
    doc_results = []
    doc_lock = threading.Lock()
    doc_first_fail = [None]  # [reason] 首个失败原因，便于诊断
    print("    [进度] 开始文件上传下载 (%d 对)... [%s]" % (len(doc_pairs), _ts()), flush=True)

    def doc_pair_one(acc_sender, acc_receiver, idx):
        fail_reason = None
        try:
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                fail_reason = "连接失败"
                with doc_lock:
                    doc_results.append((idx, False, False))
                    if doc_first_fail[0] is None:
                        doc_first_fail[0] = fail_reason
                return
            send_json(sa, {"tag": "login", "account_number": acc_sender, "password": "123456"})
            recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            send_json(sa, {"tag": "addfriend", "account": acc_sender, "friend": acc_receiver})
            r_add, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            send_json(sb, {"tag": "login", "account_number": acc_receiver, "password": "123456"})
            recv_packet(sb, timeout=RECV_TIMEOUT_SHORT)
            # duplicate = pending 申请仍在，收方必须 accept
            send_json(sb, {"tag": "newfriends", "account": acc_receiver, "sender": acc_sender, "answer": "accept"})
            recv_packet(sb, timeout=RECV_TIMEOUT_SHORT)
            file_uuid = str(uuid.uuid4())
            content = file_content
            fname = get_document_filename()
            send_json(sa, {"tag": "upload_begin", "uuid": file_uuid, "sender": acc_sender, "receiver": acc_receiver,
                          "filename": fname, "messagetype": "document",
                          "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(len(content))})
            resp, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)  # 服务端 runDbTask 可能排队
            for _ in range(10):  # 若收到登录等遗留包，继续读直到 upload_begin_ack
                if resp is None or not isinstance(resp, dict) or resp.get("tag") in ("upload_begin_ack", "upload_error"):
                    break
                resp, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            upload_ok = False
            if resp and resp.get("tag") == "upload_begin_ack":
                chunk_size = 64 * 1024
                # 文档上传分片需要比普通消息更慢一些，否则容易触发服务端背压断连（10054）
                chunk_interval = max(0.12, cfg.get("msg_interval_sec", 0.03))
                for j in range(0, len(content), chunk_size):
                    chunk = content[j:j + chunk_size]
                    send_doc_chunk(sa, file_uuid, j // chunk_size, chunk)
                    if chunk_interval > 0 and j + chunk_size < len(content):
                        time.sleep(chunk_interval)
                send_json(sa, {"tag": "upload_end", "uuid": file_uuid})
                time.sleep(0.5)  # 给服务端 upload_end 入队/处理缓冲时间
                for _ in range(UPLOAD_DONE_MAX_LOOPS):
                    resp, _ = recv_packet(sa, timeout=UPLOAD_DONE_TIMEOUT)
                    if isinstance(resp, dict):
                        tag = resp.get("tag", "")
                        if tag == "uploaddone":
                            upload_ok = True
                            break
                        if tag == "upload_error":
                            fail_reason = "upload_error: %s" % resp.get("message", "未知")
                            break
                if not upload_ok and fail_reason is None:
                    fail_reason = "未收到 uploaddone"
            else:
                if resp and resp.get("tag") == "upload_error":
                    fail_reason = "upload_begin 失败: %s" % resp.get("message", "未知")
                elif resp is None:
                    fail_reason = "未收到 upload_begin_ack（超时/连接关闭，resp=None）"
                elif isinstance(resp, dict):
                    tag = resp.get("tag", "?")
                    snippet = json.dumps(resp, ensure_ascii=False)[:200]
                    fail_reason = "未收到 upload_begin_ack（收到 tag=%s）%s" % (tag, (" 响应: %s" % snippet) if tag != "upload_begin_ack" else "")
                else:
                    fail_reason = "未收到 upload_begin_ack（收到非预期类型）"
            sa.close()
            got_begin = got_end = False
            if upload_ok:
                time.sleep(0.8)  # 等待服务端 upload_end 完成 DB 写入
                # 下载阶段允许重试：偶发断连/尾包丢失时，重连再次 askfordocument 可显著提升成功率
                for dl_try in range(3):
                    if dl_try > 0:
                        try:
                            sb.close()
                        except Exception:
                            pass
                        sb, _ = connect(host, port)
                        if not sb:
                            if fail_reason is None:
                                fail_reason = "下载重连失败"
                            continue
                        send_json(sb, {"tag": "login", "account_number": acc_receiver, "password": "123456"})
                        rb2, _ = recv_packet(sb, timeout=RECV_TIMEOUT_SHORT)
                        if not rb2 or rb2.get("answer") != "loginsucceed":
                            if fail_reason is None:
                                fail_reason = "下载重连后登录失败"
                            continue

                    got_begin = got_end = False
                    send_json(sb, {"tag": "askfordocument", "uuid": file_uuid, "account": acc_receiver})
                    for _ in range(90):
                        resp, ptype = recv_packet(sb, timeout=RECV_TIMEOUT_DEFAULT)  # 文档泵送可能较慢，用默认 45s
                        if resp is None:
                            break
                        if ptype == PAYLOAD_JSON and isinstance(resp, dict):
                            tag = resp.get("tag", "")
                            if tag == "document_begin":
                                got_begin = True
                            elif tag == "document_end":
                                got_end = True
                                break
                            elif tag == "document_error":
                                if fail_reason is None:
                                    fail_reason = "document_error: %s" % resp.get("message", "未知")
                                break
                    if got_begin and got_end:
                        break
                    if fail_reason is None:
                        fail_reason = ("收包超时或连接关闭（已收到 document_begin 后未收到 document_end）"
                                       if got_begin else "收包超时或连接关闭（未收到 document_begin）")
            sb.close()
            with doc_lock:
                doc_results.append((idx, upload_ok, got_begin and got_end))
                if doc_first_fail[0] is None and fail_reason:
                    doc_first_fail[0] = fail_reason
        except Exception as e:
            with doc_lock:
                doc_results.append((idx, False, False))
                if doc_first_fail[0] is None:
                    doc_first_fail[0] = "异常: %s" % str(e)

    t0 = time.perf_counter()
    stop_doc_progress = threading.Event()

    def _doc_progress_loop():
        last_n = -1
        while not stop_doc_progress.wait(2):
            n = len(doc_results)
            if n >= len(doc_pairs):
                break
            if n > last_n:
                last_n = n
                up_ok = sum(1 for _, u, _ in doc_results if u)
                dl_ok = sum(1 for _, _, d in doc_results if d)
                print("    [进度] 文件: 已完成 %d/%d 对, 上传成功 %d 下载成功 %d [%s]" % (n, len(doc_pairs), up_ok, dl_ok, _ts()), flush=True)

    doc_progress_thread = threading.Thread(target=_doc_progress_loop, daemon=True)
    doc_progress_thread.start()

    for start in range(0, len(doc_pairs), doc_concurrent):
        batch = min(doc_concurrent, len(doc_pairs) - start)
        print("    [进度] 文件批次 %d 对，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = [threading.Thread(target=doc_pair_one, args=(doc_pairs[start + j][0], doc_pairs[start + j][1], start + j))
                  for j in range(batch)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    stop_doc_progress.set()
    doc_upload_ok = sum(1 for _, u, _ in doc_results if u)
    doc_download_ok = sum(1 for _, _, d in doc_results if d)
    ms = (time.perf_counter() - t0) * 1000
    msg = "%d 对：上传成功 %d 失败 %d，下载成功 %d 失败 %d" % (
        len(doc_pairs), doc_upload_ok, len(doc_pairs) - doc_upload_ok, doc_download_ok, len(doc_pairs) - doc_download_ok)
    if doc_upload_ok >= len(doc_pairs) and doc_download_ok >= len(doc_pairs):
        ok(msg, ms)
    else:
        fail("%s（全部成功才通过）" % msg)
        if doc_first_fail[0]:
            print("    [诊断] 首个失败原因: %s" % doc_first_fail[0])
        return False

    size_bytes = int(size_mb * 1024 * 1024)
    upload_concurrent = cfg.get("concurrent_step3", -1)
    if upload_concurrent == -1:
        upload_concurrent = conc_uploads

    section("第三步：%d 对用户：大文件上传（源 example3.mp4，单路最多约 %g MB），对应接收方各收 1 个 (并发：%d)"
            % (conc_uploads, float(size_mb), upload_concurrent))
    upload_pairs = []
    upload_reg_ok = 0
    print("    [进度] 创建大文件用户对... [%s]" % _ts(), flush=True)
    for i in range(conc_uploads):
        sa, _ = connect(host, port)
        sb, _ = connect(host, port)
        if not sa or not sb:
            if sa:
                sa.close()
            if sb:
                sb.close()
            if (i + 1) % 4 == 0 or i == 0:
                print("    [进度] 大文件用户: 已创建 %d/%d 对, 成功 %d 失败 %d [%s]" % (i + 1, conc_uploads, upload_reg_ok, (i + 1) - upload_reg_ok, _ts()), flush=True)
            continue
        send_json(sa, {"tag": "register", "nickname": "上传发%d_%s" % (i + 1, suffix), "gender": "男",
                       "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
        ra, _ = recv_packet(sa, timeout=RECV_TIMEOUT_LOGIN)
        send_json(sb, {"tag": "register", "nickname": "上传收%d_%s" % (i + 1, suffix), "gender": "女",
                       "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
        rb, _ = recv_packet(sb, timeout=RECV_TIMEOUT_LOGIN)
        sa.close()
        sb.close()
        if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
            upload_pairs.append((ra.get("account_number"), rb.get("account_number")))
            upload_reg_ok += 1
        if (i + 1) % 4 == 0 or i == 0:
            print("    [进度] 大文件用户: 已创建 %d/%d 对, 成功 %d 失败 %d [%s]" % (i + 1, conc_uploads, upload_reg_ok, (i + 1) - upload_reg_ok, _ts()), flush=True)
    print("    [进度] 大文件用户创建完成: 成功 %d 对 失败 %d 对" % (upload_reg_ok, conc_uploads - upload_reg_ok))
    _upe = max(1, len(upload_pairs) // 3) if len(upload_pairs) > 1 else 1
    print("    [进度] 大文件用户加好友 (并发：%d)... [%s]" % (ADDFRIEND_PAIR_CONCURRENT, _ts()), flush=True)
    addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
        host, port, upload_pairs, ADDFRIEND_PAIR_CONCURRENT, _upe, "大文件加好友")
    ok_pairs = min(addfriend_ok, newfriends_ok)
    print("    [进度] 大文件加好友完成: 成功 %d 对，失败 %d 对，共 %d 对" % (ok_pairs, len(upload_pairs) - ok_pairs, len(upload_pairs)))
    print("    [进度] 开始大文件上传 (%d 对，单路负载上限约 %g MB)... [%s]" % (len(upload_pairs), float(size_mb), _ts()), flush=True)
    results = []
    lock = threading.Lock()
    upload_first_fail = [None]  # [reason] 首个大文件上传失败原因，便于诊断

    def upload_one(acc_sender, acc_receiver, idx):
        s = None
        fail_reason = None
        try:
            s, _ = connect(host, port)
            if not s:
                with lock:
                    results.append((idx, False, 0))
                    if upload_first_fail[0] is None:
                        upload_first_fail[0] = "连接失败"
                return
            send_json(s, {"tag": "login", "account_number": acc_sender, "password": "123456"})
            recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            file_uuid = str(uuid.uuid4())
            file_content = get_upload_content(size_bytes)
            if not file_content:
                with lock:
                    results.append((idx, False, 0))
                s.close()
                return
            t0 = time.perf_counter()
            fname = os.path.basename(RES_VIDEO) if len(file_content) > 10*1024*1024 and _load_file(RES_VIDEO) else (os.path.basename(RES_IMAGE) if _load_file(RES_IMAGE) else "test_%d.bin" % (idx + 1))
            actual_size = len(file_content)
            send_json(s, {"tag": "upload_begin", "uuid": file_uuid, "sender": acc_sender, "receiver": acc_receiver,
                          "filename": fname, "messagetype": "document",
                          "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(actual_size)})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            for _ in range(10):  # 若收到登录等遗留包，继续读直到 upload_begin_ack
                if resp is None or not isinstance(resp, dict) or resp.get("tag") in ("upload_begin_ack", "upload_error"):
                    break
                resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            if not resp or resp.get("tag") != "upload_begin_ack":
                with lock:
                    results.append((idx, False, 0))
                    if upload_first_fail[0] is None:
                        upload_first_fail[0] = "未收到 upload_begin_ack"
                s.close()
                return
            chunk_size = 64 * 1024
            chunk_interval = cfg.get("upload_chunk_interval_sec", cfg.get("msg_interval_sec", 0.03))
            for i in range(0, actual_size, chunk_size):
                chunk = file_content[i:i + chunk_size]
                send_doc_chunk(s, file_uuid, i // chunk_size, chunk)
                if chunk_interval > 0 and i + chunk_size < actual_size:
                    time.sleep(chunk_interval)
            send_json(s, {"tag": "upload_end", "uuid": file_uuid})
            done = False
            wait_deadline = time.perf_counter() + 600  # 大文件最多等 10 分钟
            while time.perf_counter() < wait_deadline:
                resp, _ = recv_packet(s, timeout=UPLOAD_DONE_TIMEOUT)
                if isinstance(resp, dict) and resp.get("tag") == "uploaddone":
                    done = True
                    break
            if not done and fail_reason is None:
                fail_reason = "未收到 uploaddone"
            elapsed = time.perf_counter() - t0
            s.close()
            with lock:
                results.append((idx, done, elapsed))
                if not done and upload_first_fail[0] is None:
                    upload_first_fail[0] = fail_reason
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            if s:
                try:
                    s.close()
                except Exception:
                    pass
            with lock:
                results.append((idx, False, 0))
                if upload_first_fail[0] is None:
                    upload_first_fail[0] = "连接被服务端断开/重置(10054/ECONNRESET等)"

    upload_start = time.perf_counter()
    stop_upload_progress = threading.Event()

    def _upload_progress_loop():
        last_n = -1
        while not stop_upload_progress.wait(3):
            n = len(results)
            if n >= len(upload_pairs):
                break
            if n > last_n:
                last_n = n
                ok_cnt = sum(1 for _, done, _ in results if done)
                print("    [进度] 大文件: 已完成 %d/%d 对, 成功 %d 失败 %d [%s]" % (n, len(upload_pairs), ok_cnt, n - ok_cnt, _ts()), flush=True)

    upload_progress_thread = threading.Thread(target=_upload_progress_loop, daemon=True)
    upload_progress_thread.start()

    for start in range(0, len(upload_pairs), upload_concurrent):
        batch = min(upload_concurrent, len(upload_pairs) - start)
        print("    [进度] 大文件批次 %d 对，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = []
        for j in range(batch):
            i = start + j
            acc_s, acc_r = upload_pairs[i]
            t = threading.Thread(target=upload_one, args=(acc_s, acc_r, i))
            threads.append(t)
            t.start()
        for t in threads:
            t.join()
    stop_upload_progress.set()
    upload_elapsed = time.perf_counter() - upload_start
    ok_cnt = sum(1 for _, done, _ in results if done)
    total_mb = len(upload_pairs) * size_mb
    speed_mbps = total_mb / upload_elapsed if upload_elapsed > 0 else 0
    msg = "%d 对：成功 %d 失败 %d, 总耗时 %.1f ms, 总速度 %.2f MB/s" % (
        len(upload_pairs), ok_cnt, len(upload_pairs) - ok_cnt, upload_elapsed * 1000, speed_mbps)
    if ok_cnt >= len(upload_pairs):
        ok(msg)
    else:
        fail("%s（全部成功才通过）" % msg)
        if upload_first_fail[0]:
            print("    [诊断] 首个失败原因: %s" % upload_first_fail[0])
        return False

    step4_concurrent = cfg.get("concurrent_step4", -1)
    if step4_concurrent == -1:
        step4_concurrent = n_pairs
    step4_concurrent = max(1, min(int(step4_concurrent), n_pairs))
    section("第四步：%d 对：发方修改密码、新密码登录、删除好友 (并发：%d)" % (n_pairs, step4_concurrent))
    print("    [进度] 改密、删好友进行中... [%s]" % _ts(), flush=True)
    step1314_ok = 0
    step1314_lock = threading.Lock()
    step1314_done = [0]

    def run_step1314(acc_s, acc_r):
        nonlocal step1314_ok
        try:
            s, _ = connect(host, port)
            if not s:
                return
            send_json(s, {"tag": "login", "account_number": acc_s, "password": "123456"})
            recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            # changepassword1 验证旧密码（覆盖未覆盖接口）
            send_json(s, {"tag": "changepassword1", "account": acc_s, "password": "123456"})
            resp1, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            if not resp1 or resp1.get("answer") != "succeed":
                s.close()
                return
            send_json(s, {"tag": "changepassword2", "account": acc_s, "password": "123456", "newpassword": "654321"})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            s.close()
            if not resp or resp.get("answer") != "succeed":
                return
            s, _ = connect(host, port)
            if not s:
                return
            send_json(s, {"tag": "login", "account_number": acc_s, "password": "654321"})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            if not resp or resp.get("answer") != "loginsucceed":
                s.close()
                return
            send_json(s, {"tag": "deletefriend", "account": acc_s, "friend": acc_r})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            s.close()
            if resp and resp.get("tag") == "deletefriendsucceed":
                with step1314_lock:
                    step1314_ok += 1
        finally:
            with step1314_lock:
                step1314_done[0] += 1

    stop_s1314 = threading.Event()

    def _s1314_progress():
        while not stop_s1314.wait(2):
            d = step1314_done[0]
            if d >= n_pairs:
                break
            if d > 0:
                print("    [进度] 改密删好友: 已完成 %d/%d 对, 成功 %d 失败 %d [%s]" % (d, n_pairs, step1314_ok, d - step1314_ok, _ts()), flush=True)

    s1314_prog = threading.Thread(target=_s1314_progress, daemon=True)
    s1314_prog.start()

    for start in range(0, n_pairs, step4_concurrent):
        batch = min(step4_concurrent, n_pairs - start)
        print("    [进度] 改密删友批次 %d 对，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = [threading.Thread(target=run_step1314, args=main_pairs[start + j]) for j in range(batch)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    stop_s1314.set()
    if step1314_ok == n_pairs:
        ok("%d 对全部完成改密、删好友" % n_pairs, None)
    else:
        fail("第四步仅 %d/%d 对成功" % (step1314_ok, n_pairs))

    # 收集所有账号：(account, password)
    logout_list = []
    for acc_s, acc_r in main_pairs:
        logout_list.append((acc_s, "654321"))   # 发方已改密
        logout_list.append((acc_r, "123456"))   # 收方未改密
    for acc_s, acc_r in doc_pairs:
        logout_list.append((acc_s, "123456"))
        logout_list.append((acc_r, "123456"))
    for acc_s, acc_r in upload_pairs:
        logout_list.append((acc_s, "123456"))
        logout_list.append((acc_r, "123456"))
    logout_ok = 0
    logout_lock = threading.Lock()

    logout_fail_detail = []  # 记录失败详情便于诊断
    logout_fail_accounts = []  # 失败账号（与 fail_detail 同步追加，便于末尾汇总）

    def _logout_one(acc, pwd):
        nonlocal logout_ok
        acc_w = normalize_account_key(acc)
        if not acc_w:
            with logout_lock:
                mark = repr(acc)
                logout_fail_detail.append("%s: 账号归一化后为空，跳过注销" % mark)
                logout_fail_accounts.append(mark)
            return
        s, _ = connect(host, port)
        if not s:
            with logout_lock:
                logout_fail_detail.append("%s: 连接失败" % acc_w)
                logout_fail_accounts.append(acc_w)
            return
        try:
            send_json(s, {"tag": "login", "account_number": acc_w, "password": pwd})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            if resp and resp.get("answer") == "loginsucceed":
                send_json(s, {"tag": "logout", "account": acc_w, "password": pwd})
                resp2, _ = recv_packet(s, timeout=RECV_TIMEOUT_LOGOUT)
                # 登录后服务端可能先推 heart/业务包，再回 logout；每步用 RECV_TIMEOUT_LOGOUT，避免线程池慢时误判失败
                for _ in range(48):
                    if resp2 is None or not isinstance(resp2, dict) or resp2.get("tag") == "logout":
                        break
                    resp2, _ = recv_packet(s, timeout=RECV_TIMEOUT_LOGOUT)
                if resp2 and resp2.get("tag") == "logout" and resp2.get("answer") == "success":
                    with logout_lock:
                        logout_ok += 1
                else:
                    with logout_lock:
                        logout_fail_accounts.append(acc_w)
                        if isinstance(resp2, dict):
                            logout_fail_detail.append(
                                "%s: 注销未成功 tag=%s answer=%s reason=%s"
                                % (acc_w, resp2.get("tag", "?"), resp2.get("answer", "?"), resp2.get("reason", "")))
                        else:
                            logout_fail_detail.append("%s: 注销无有效 JSON 响应 %r" % (acc_w, resp2))
            else:
                with logout_lock:
                    logout_fail_detail.append("%s: 登录失败" % acc_w)
                    logout_fail_accounts.append(acc_w)
        except Exception as e:
            with logout_lock:
                logout_fail_detail.append("%s: 异常 %s" % (acc_w, str(e)))
                logout_fail_accounts.append(acc_w)
        finally:
            s.close()

    step5_concurrent = cfg.get("concurrent_step5", -1)
    if step5_concurrent == -1:
        step5_concurrent = len(logout_list)
    step5_concurrent = max(1, min(int(step5_concurrent), len(logout_list))) if logout_list else 1
    section("第五步：%d 个账号注销 (并发：%d)" % (len(logout_list), step5_concurrent))
    print("    [注销清单] 共 %d 个账号（序号 / 账号 / 密码）：" % len(logout_list), flush=True)
    for i, (acc, pwd) in enumerate(logout_list, 1):
        aw = normalize_account_key(acc)
        print("      %d. %s  %s" % (i, aw if aw else repr(acc), pwd), flush=True)
    for start in range(0, len(logout_list), step5_concurrent):
        batch = logout_list[start:start + step5_concurrent]
        print("    [进度] 注销批次 %d 个账号，起始 %d [%s]" % (len(batch), start, _ts()), flush=True)
        threads = [threading.Thread(target=_logout_one, args=(acc, pwd)) for acc, pwd in batch]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    total_elapsed = (time.perf_counter() - total_start) * 1000
    if logout_ok == len(logout_list):
        ok("全部 %d 个用户注销成功" % logout_ok)
        section("测试完成")
        print("  全部通过")
        print("  总耗时: %.1f ms (%.2f 秒)" % (total_elapsed, total_elapsed / 1000))
        return True
    else:
        fail("注销未全部成功：成功 %d / 共 %d 个账号" % (logout_ok, len(logout_list)))
        for d in logout_fail_detail[:5]:  # 最多打印 5 条
            print("    [诊断] %s" % d, flush=True)
        if logout_fail_accounts:
            _failed_uq = list(dict.fromkeys(logout_fail_accounts))
            print("    [注销失败账号] 共 %d 个：" % len(_failed_uq), flush=True)
            for fa in _failed_uq:
                print("      %s" % fa, flush=True)
        section("测试完成")
        print("  第五步注销失败，未全部通过")
        print("  总耗时: %.1f ms (%.2f 秒)" % (total_elapsed, total_elapsed / 1000))
        return False


def fmt_size(n):
    if n < 1024:
        return "%d B" % n
    if n < 1024 * 1024:
        return "%.1f KB" % (n / 1024)
    return "%.2f MB" % (n / (1024 * 1024))


def run_batch_register(host, port, count, concurrent=None):
    batch = concurrent if concurrent is not None else REG.get("batch", 8)
    section("批量注册：%d 个 (并发：%d)" % (count, batch))
    suffix = str(int(time.time() * 1000))[-6:]
    results = []
    lock = threading.Lock()

    def reg_one(i):
        acc, ms = None, 0
        for attempt in range(REG.get("retries", 5)):
            s, _ = connect(host, port)
            if not s:
                time.sleep(REG.get("retry_sleep", 0.5) * (attempt + 1))
                continue
            t0 = time.perf_counter()
            send_json(s, {"tag": "register", "nickname": "批量_%d_%s" % (i + 1, suffix), "gender": "男",
                          "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            resp, _ = recv_packet(s, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            ms = (time.perf_counter() - t0) * 1000
            s.close()
            if resp and resp.get("answer") == "regissucceed":
                acc = resp.get("account_number")
                break
            time.sleep(REG.get("retry_sleep", 0.5) * (attempt + 1))
        with lock:
            results.append((i, acc is not None, ms, acc))

    t0 = time.perf_counter()
    stop_prog = threading.Event()
    def _reg_progress():
        while not stop_prog.wait(2):
            n = len(results)
            if n >= count:
                break
            ok_cnt = sum(1 for _, ok, _, _ in results if ok)
            print("    [进度] 注册: 已完成 %d/%d, 成功 %d 失败 %d [%s]" % (n, count, ok_cnt, n - ok_cnt, _ts()), flush=True)
    t_prog = threading.Thread(target=_reg_progress, daemon=True)
    t_prog.start()
    for start in range(0, count, batch):
        b = min(batch, count - start)
        print("    [进度] 注册批次 %d 个，起始 %d [%s]" % (b, start, _ts()), flush=True)
        threads = [threading.Thread(target=reg_one, args=(start + j,)) for j in range(b)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        if start + b < count and REG.get("batch_interval", 0.5) > 0:
            time.sleep(REG.get("batch_interval", 0.5))
    stop_prog.set()
    ok_cnt = sum(1 for _, ok, _, _ in results if ok)
    latencies = [ms for _, ok, ms, _ in results if ok and ms > 0]
    accounts = [acc for _, ok, _, acc in results if ok and acc]
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d, 失败 %d, 总耗时 %.1f ms" % (ok_cnt, count - ok_cnt, elapsed))
    if latencies:
        print("  延迟: 最小 %.1f ms, 最大 %.1f ms, 平均 %.1f ms" %
              (min(latencies), max(latencies), sum(latencies) / len(latencies)))
    return accounts


def run_batch_message(host, port, count, concurrent=None, pre_created_accounts=None):
    cfg_msg_conc = concurrent if concurrent is not None else MSG.get("concurrent", -1)
    c = cfg_msg_conc
    if c == -1:
        c = 9999  # 全并发时用大数
    if pre_created_accounts and len(pre_created_accounts) >= 2:
        num_pairs = len(pre_created_accounts) // 2
        need = 2 * num_pairs
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
        msg_per_pair = max(1, count // num_pairs)   # all 模式 count=总条数
    else:
        num_pairs = min(max(1, count), c * 4)   # 单独测试时 count=配置的 pairs
        msg_per_pair = MSG.get("msg_per_pair", 5)
        need = 2 * num_pairs
        pairs = []
    section("批量消息：计划 %d 对，每对 %d 条，共 %d 条 %s"
            % (num_pairs, msg_per_pair, num_pairs * msg_per_pair, conc_paren(cfg_msg_conc, num_pairs)))
    if pre_created_accounts and pairs:
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 对 [%s]" % (need, len(pairs), _ts()), flush=True)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        print("    [进度] 创建消息用户对... [%s]" % _ts(), flush=True)
        for i in range(num_pairs):
            if (i + 1) % 20 == 0 or i == 0:
                print("    [进度] 消息用户: 尝试 %d/%d，成功 %d 对 [%s]" % (i + 1, num_pairs, len(pairs), _ts()), flush=True)
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                continue
            send_json(sa, {"tag": "register", "nickname": "消息发%d_%s" % (i + 1, suffix), "gender": "男", "password": "123456",
                           "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            send_json(sb, {"tag": "register", "nickname": "消息收%d_%s" % (i + 1, suffix), "gender": "女", "password": "123456",
                           "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                pairs.append((ra.get("account_number"), rb.get("account_number")))
    if not pairs:
        fail("无可用用户对")
        return
    if pre_created_accounts:
        msg_per_pair = max(1, count // len(pairs))   # all 模式 count=总条数，按实际对数调整
    # 单独测试时 msg_per_pair 已从配置读取，不覆盖
    if len(pairs) < num_pairs:
        print("    [进度] 消息用户: 计划 %d 对，成功创建 %d 对 [%s]" % (num_pairs, len(pairs), _ts()), flush=True)
    pe = MSG.get("progress_every", 20)
    print("    [进度] 消息用户加好友 (并发：%d)... [%s]" % (ADDFRIEND_PAIR_CONCURRENT, _ts()), flush=True)
    addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
        host, port, pairs, ADDFRIEND_PAIR_CONCURRENT, pe, "消息加好友")
    ok_pairs = min(addfriend_ok, newfriends_ok)
    print("    [进度] 消息加好友完成: 成功 %d 对，失败 %d 对，共 %d 对" % (ok_pairs, len(pairs) - ok_pairs, len(pairs)))
    print("    [进度] 开始发消息 (%d 对，每对 %d 条 %s)... [%s]"
          % (len(pairs), msg_per_pair, conc_paren(cfg_msg_conc, len(pairs)), _ts()), flush=True)
    results = []
    lock = threading.Lock()

    def msg_pair_one(acc_s, acc_r, idx, n_msg):
        ok_cnt = 0
        try:
            s, _ = connect(host, port)
            if not s:
                return
            send_json(s, {"tag": "login", "account_number": acc_s, "password": "123456"})
            recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            for i in range(n_msg):
                send_json(s, {"tag": "messages", "sender": acc_s, "receiver": acc_r, "messagetype": "text",
                               "uuid": str(uuid.uuid4()), "messages": "批量消息_%d_%d" % (idx + 1, i + 1), "filename": ""})
                resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
                if resp and (resp.get("tag") in ("ack", "messagehavedone") or "messages" in str(resp)):
                    ok_cnt += 1
            s.close()
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass
        finally:
            with lock:
                results.append((idx, ok_cnt, n_msg))

    t0 = time.perf_counter()
    stop_msg = threading.Event()

    def _msg_progress():
        last_n = -1
        while not stop_msg.wait(2):
            n = len(results)
            if n >= len(pairs):
                break
            if n > last_n:
                last_n = n
                total_ok = sum(r[1] for r in results)
                total_msg = sum(r[2] for r in results)
                print("    [进度] 消息: 已完成 %d/%d 对, 成功 %d 条 失败 %d 条 [%s]" % (n, len(pairs), total_ok, total_msg - total_ok, _ts()), flush=True)

    msg_prog = threading.Thread(target=_msg_progress, daemon=True)
    msg_prog.start()

    threads = []
    for i, (acc_s, acc_r) in enumerate(pairs):
        t = threading.Thread(target=msg_pair_one, args=(acc_s, acc_r, i, msg_per_pair))
        threads.append(t)
        t.start()
    for t in threads:
        t.join()
    stop_msg.set()
    total_ok = sum(r[1] for r in results)
    total_msg = sum(r[2] for r in results)
    ms = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d/%d 条, 失败 %d 条, %d 对, 总耗时 %.1f ms, 平均 %.1f ms/条" %
          (total_ok, total_msg, total_msg - total_ok, len(pairs), ms, ms / total_msg if total_msg else 0))


def run_batch_friend(host, port, count, pre_created_accounts=None):
    """N 个用户加 1 个中心用户为好友"""
    section("批量好友：%d 人加 1 人 %s" % (count, conc_paren(FRIEND.get("concurrent", ADDFRIEND_PAIR_CONCURRENT))))
    accs = []
    ok_cnt = 0  # 下面各分支会赋值；避免未定义
    ms = 0.0
    stats_reuse_addfriend = None  # 仅「复用 reg」路径：(duplicate 次数, ok 次数, updatefriendship 次数)
    if pre_created_accounts and len(pre_created_accounts) >= count + 1:
        acc_center = pre_created_accounts[0]
        accs = list(pre_created_accounts[1:1 + count])
        print("    [复用] 使用 reg 阶段账号：中心 + %d 人 [%s]" % (len(accs), _ts()), flush=True)
        conc_f = max(1, min(int(FRIEND.get("concurrent", ADDFRIEND_PAIR_CONCURRENT)), len(accs)))
        print("    [进度] 添加好友请求 (并发：%d)... [%s]" % (conc_f, _ts()), flush=True)
        addfriend_ok = 0
        n_add_dup = 0  # addfriend 应答 duplicate
        n_add_ok = 0  # addfriend 应答 ok
        n_pair_done = 0  # 第二元为 1：收到 updatefriendship
        pe = FRIEND.get("progress_every", 20)
        t0_reuse = time.perf_counter()
        results_lock = threading.Lock()
        done = [0]

        def add_one(acc):
            nonlocal addfriend_ok, n_add_dup, n_add_ok, n_pair_done
            a, n_nf, ad = add_friend_pair(host, port, acc, acc_center)
            with results_lock:
                addfriend_ok += a
                if ad == "duplicate":
                    n_add_dup += 1
                elif ad == "ok":
                    n_add_ok += 1
                if n_nf == 1:
                    n_pair_done += 1
                done[0] += 1
                d = done[0]
                if pe and (d % pe == 0 or d == 1 or d == len(accs)):
                    print("    [进度] 好友: 已添加 %d/%d, 成功 %d 失败 %d [%s]" % (d, len(accs), addfriend_ok, d - addfriend_ok, _ts()), flush=True)

        for start in range(0, len(accs), conc_f):
            threads = [threading.Thread(target=add_one, args=(accs[i],)) for i in range(start, min(start + conc_f, len(accs)))]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
        print("    [进度] 添加好友请求完成: 成功 %d 失败 %d（add_friend_pair 已含接受）" % (addfriend_ok, len(accs) - addfriend_ok))
        ok_cnt = addfriend_ok
        ms = (time.perf_counter() - t0_reuse) * 1000
        stats_reuse_addfriend = (n_add_dup, n_add_ok, n_pair_done)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        resp = None
        for attempt in range(5):
            center_sock, _ = connect(host, port)
            if not center_sock:
                time.sleep(0.5 * (attempt + 1))
                continue
            send_json(center_sock, {"tag": "register", "nickname": "中心_" + suffix, "gender": "男", "password": "123456",
                                    "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            resp, _ = recv_packet(center_sock, timeout=RECV_TIMEOUT_DEFAULT)
            center_sock.close()
            if resp and resp.get("answer") == "regissucceed":
                break
            time.sleep(1.0 * (attempt + 1))
        if not resp or resp.get("answer") != "regissucceed":
            fail("中心用户注册失败")
            return
        acc_center = resp.get("account_number")
        lock = threading.Lock()

        def reg_and_add(i):
            for attempt in range(3):
                s, _ = connect(host, port)
                if not s:
                    time.sleep(0.1)
                    continue
                try:
                    send_json(s, {"tag": "register", "nickname": "好友_%d_%s" % (i + 1, suffix), "gender": "男",
                              "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
                    r, _ = recv_packet(s, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                    if not r or r.get("answer") != "regissucceed":
                        s.close()
                        time.sleep(0.05)
                        continue
                    acc = r.get("account_number")
                    send_json(s, {"tag": "login", "account_number": acc, "password": "123456"})
                    recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
                    send_json(s, {"tag": "addfriend", "account": acc, "friend": acc_center})
                    recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
                    s.close()
                    with lock:
                        accs.append(acc)
                    return
                except Exception:
                    if s:
                        try:
                            s.close()
                        except Exception:
                            pass
                    time.sleep(0.05)

        print("    [进度] 注册并添加好友 (并发：%d)... [%s]" % (count, _ts()), flush=True)
        stop_prog = threading.Event()
        def _friend_progress():
            while not stop_prog.wait(2):
                n = len(accs)
                if n >= count:
                    break
                print("    [进度] 好友: 已注册添加 %d/%d（成功 %d） [%s]" % (n, count, n, _ts()), flush=True)
        t_prog = threading.Thread(target=_friend_progress, daemon=True)
        t_prog.start()
        threads = [threading.Thread(target=reg_and_add, args=(j,)) for j in range(count)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        stop_prog.set()
    n_accs = len(accs)
    if n_accs < count:
        print("    [提示] 注册+加好友成功 %d/%d，失败 %d 个" % (n_accs, count, count - n_accs), flush=True)
    if not (pre_created_accounts and len(pre_created_accounts) >= count + 1):
        print("    [进度] 中心用户接受好友 (%d 个) [%s]..." % (n_accs, _ts()), flush=True)
        center_sock, _ = connect(host, port)
        send_json(center_sock, {"tag": "login", "account_number": acc_center, "password": "123456"})
        recv_packet(center_sock, timeout=RECV_TIMEOUT_SHORT)
        t0 = time.perf_counter()
        ok_cnt = 0
        for idx, acc in enumerate(accs):
            send_json(center_sock, {"tag": "newfriends", "account": acc_center, "sender": acc, "answer": "accept"})
            resp, _ = recv_packet(center_sock, timeout=RECV_TIMEOUT_RELAXED)
            if resp and resp.get("tag") == "updatefriendship":
                ok_cnt += 1
            if (idx + 1) % 100 == 0 or idx == 0:
                print("    [进度] 接受好友: 已完成 %d/%d, 成功 %d 失败 %d [%s]" % (idx + 1, len(accs), ok_cnt, (idx + 1) - ok_cnt, _ts()), flush=True)
        ms = (time.perf_counter() - t0) * 1000
        center_sock.close()
    print("-" * 50)
    print("  结果: 成功 %d/%d 个好友, 失败 %d 个, 总耗时 %.1f ms" % (ok_cnt, len(accs), len(accs) - ok_cnt, ms))
    if stats_reuse_addfriend is not None:
        d, o, p = stats_reuse_addfriend
        print("  说明（复用 reg 账号）: addfriend 应答 duplicate=%d 次、ok=%d 次；"
              "收方 newfriends 后收到 updatefriendship=%d 次（脚本侧认为好友链已就绪的对数）。" % (d, o, p))
    else:
        print("  说明（现注册路径）: 上面「总耗时」仅为「中心账号逐个 newfriends 接受」阶段；"
              "前面多连接并发注册+addfriend 的耗时不计入此毫秒数。")


def run_findpassword(host, port, count, pre_created_accounts=None):
    conc = FINDPWD.get("concurrent", -1)
    section("批量找回密码：%d 次 %s" % (count, conc_paren(conc, count)))
    accs = []
    if pre_created_accounts and len(pre_created_accounts) >= count:
        accs = list(pre_created_accounts[:count])
        print("    [复用] 使用 reg 阶段 %d 个账号 [%s]" % (len(accs), _ts()), flush=True)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        print("    [进度] 创建找密用户... [%s]" % _ts(), flush=True)
        for i in range(count):
            if (i + 1) % 50 == 0 or i == 0:
                print("    [进度] 找密用户: 已注册 %d/%d（成功 %d） [%s]" % (i + 1, count, len(accs), _ts()), flush=True)
            for attempt in range(3):
                s, _ = connect(host, port)
                if not s:
                    time.sleep(0.1)
                    continue
                try:
                    send_json(s, {"tag": "register", "nickname": "找密_%d_%s" % (i + 1, suffix), "gender": "男",
                              "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
                    r, _ = recv_packet(s, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                    s.close()
                    if r and r.get("answer") == "regissucceed":
                        accs.append(r.get("account_number"))
                        break
                except Exception:
                    if s:
                        try:
                            s.close()
                        except Exception:
                            pass
                time.sleep(0.05)
        if len(accs) < count:
            print("    [提示] 注册成功 %d/%d，失败 %d 个" % (len(accs), count, count - len(accs)), flush=True)
    reg_ok = len(accs)
    print("    [进度] 开始找回密码 (%d 个)... [%s]" % (reg_ok, _ts()), flush=True)
    results = []
    lock = threading.Lock()

    def findpwd_one(acc, idx):
        ok, ms = False, 0
        try:
            s, _ = connect(host, port)
            if not s:
                return
            t0 = time.perf_counter()
            send_json(s, {"tag": "findpassword1", "account_number": acc})
            r1, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            if not r1 or r1.get("answer") != "yes":
                s.close()
                return
            send_json(s, {"tag": "findpassword2", "account_number": acc, "theanswer": "a"})
            r2, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            if not r2 or r2.get("answer") != "yes":
                s.close()
                return
            send_json(s, {"tag": "findpassword3", "account_number": acc, "theanswer": "a", "password": "654321"})
            r3, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            ms = (time.perf_counter() - t0) * 1000
            s.close()
            ok = r3 and r3.get("answer") == "yes"
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass
        finally:
            with lock:
                results.append((idx, ok, ms))

    t0 = time.perf_counter()
    stop_prog = threading.Event()
    def _findpwd_progress():
        while not stop_prog.wait(2):
            n = len(results)
            if n >= len(accs):
                break
            ok_cnt = sum(1 for _, ok, _ in results if ok)
            print("    [进度] 找密: 已完成 %d/%d, 成功 %d 失败 %d [%s]" % (n, len(accs), ok_cnt, n - ok_cnt, _ts()), flush=True)
    t_prog = threading.Thread(target=_findpwd_progress, daemon=True)
    t_prog.start()
    conc = FINDPWD.get("concurrent", -1)
    if conc == -1:
        conc = len(accs)
    for start in range(0, len(accs), conc):
        batch = min(conc, len(accs) - start)
        print("    [进度] 找密码批次 %d 个，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = [threading.Thread(target=findpwd_one, args=(accs[start + j], start + j)) for j in range(batch)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    stop_prog.set()
    ok_cnt = sum(1 for _, ok, _ in results if ok)
    latencies = [ms for _, ok, ms in results if ok and ms > 0]
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 注册 %d/%d, 找回成功 %d, 失败 %d, 总耗时 %.1f ms" % (reg_ok, count, ok_cnt, reg_ok - ok_cnt, elapsed))
    if latencies:
        print("  延迟: 最小 %.1f ms, 最大 %.1f ms, 平均 %.1f ms" %
              (min(latencies), max(latencies), sum(latencies) / len(latencies)))


def run_changeinfo(host, port, count, pre_created_accounts=None, concurrent=None):
    batch = concurrent if concurrent is not None else CHANGEINFO.get("concurrent", 8)
    section("批量修改资料：%d 次 (并发：%d)" % (count, batch))
    accs = []
    if pre_created_accounts and len(pre_created_accounts) >= count:
        accs = list(pre_created_accounts[:count])
        print("    [复用] 使用 reg 阶段 %d 个账号 [%s]" % (len(accs), _ts()), flush=True)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        print("    [进度] 创建改资用户... [%s]" % _ts(), flush=True)
        for i in range(count):
            if (i + 1) % 50 == 0 or i == 0:
                print("    [进度] 改资用户: 已注册 %d/%d [%s]" % (i + 1, count, _ts()), flush=True)
            s, _ = connect(host, port)
            if not s:
                continue
            send_json(s, {"tag": "register", "nickname": "改资_%d_%s" % (i + 1, suffix), "gender": "男",
                          "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            r, _ = recv_packet(s, timeout=RECV_TIMEOUT_INSTANT)
            s.close()
            if r and r.get("answer") == "regissucceed":
                accs.append(r.get("account_number"))
        if len(accs) < count:
            print("    [进度] 改资用户: 已注册 %d/%d，成功 %d 个 [%s]" % (count, count, len(accs), _ts()), flush=True)
    print("    [进度] 开始修改资料 (%d 个)... [%s]" % (len(accs), _ts()), flush=True)
    results = []
    lock = threading.Lock()

    def change_one(acc, idx):
        ok, ms = False, 0
        try:
            s, _ = connect(host, port)
            if not s:
                return
            send_json(s, {"tag": "login", "account_number": acc, "password": "123456"})
            recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            t0 = time.perf_counter()
            send_json(s, {"tag": "changeinformation", "account": acc, "nickname": "新昵称_%d" % (idx + 1),
                          "gender": "男", "signature": "新签名_%d" % (idx + 1), "friendIds": [], "avator": AVATAR_BASE64, "avator_changed": True})
            r, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            ms = (time.perf_counter() - t0) * 1000
            s.close()
            ok = r and r.get("answer") == "succeed"
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass
        finally:
            with lock:
                results.append((idx, ok, ms))

    t0 = time.perf_counter()
    stop_prog = threading.Event()
    pe = CHANGEINFO.get("progress_every", 10)
    last_printed = [-1]
    def _change_progress():
        while not stop_prog.wait(2):
            n = len(results)
            if n >= len(accs):
                break
            if n > last_printed[0] and (n % pe == 0 or n == len(accs)):
                last_printed[0] = n
                ok_cnt = sum(1 for _, ok, _ in results if ok)
                print("    [进度] 改资: 已完成 %d/%d, 成功 %d 失败 %d [%s]" % (n, len(accs), ok_cnt, n - ok_cnt, _ts()), flush=True)
    t_prog = threading.Thread(target=_change_progress, daemon=True)
    t_prog.start()
    for start in range(0, len(accs), batch):
        b = min(batch, len(accs) - start)
        print("    [进度] 改资料批次 %d 个，起始 %d [%s]" % (b, start, _ts()), flush=True)
        threads = [threading.Thread(target=change_one, args=(accs[start + j], start + j)) for j in range(b)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    stop_prog.set()
    ok_cnt = sum(1 for _, ok, _ in results if ok)
    latencies = [ms for _, ok, ms in results if ok and ms > 0]
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d, 失败 %d, 总耗时 %.1f ms" % (ok_cnt, len(accs) - ok_cnt, elapsed))
    if latencies:
        print("  延迟: 最小 %.1f ms, 最大 %.1f ms, 平均 %.1f ms" %
              (min(latencies), max(latencies), sum(latencies) / len(latencies)))


def run_deletefriend(host, port, count, pre_created_accounts=None):
    conc = DELETEFRIEND.get("concurrent", -1)
    section("批量删除好友：计划 %d 对 %s" % (count, conc_paren(conc, count)))
    pairs = []
    need = 2 * count
    if pre_created_accounts and len(pre_created_accounts) >= need:
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 对 [%s]" % (need, len(pairs), _ts()), flush=True)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        print("    [进度] 创建删友用户对... [%s]" % _ts(), flush=True)
        for i in range(count):
            if (i + 1) % 50 == 0 or i == 0:
                print("    [进度] 删友用户: 尝试 %d/%d，成功 %d 对 [%s]" % (i + 1, count, len(pairs), _ts()), flush=True)
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                continue
            send_json(sa, {"tag": "register", "nickname": "删友发_%d_%s" % (i + 1, suffix), "gender": "男",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            send_json(sb, {"tag": "register", "nickname": "删友收_%d_%s" % (i + 1, suffix), "gender": "女",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                pairs.append((ra.get("account_number"), rb.get("account_number")))
        if len(pairs) < count:
            print("    [进度] 删友用户: 计划 %d 对，成功创建 %d 对 [%s]" % (count, len(pairs), _ts()), flush=True)
    pe = DELETEFRIEND.get("progress_every", 50)
    print("    [进度] 删友用户加好友 (并发：%d)... [%s]" % (ADDFRIEND_PAIR_CONCURRENT, _ts()), flush=True)
    addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
        host, port, pairs, ADDFRIEND_PAIR_CONCURRENT, pe, "删友加好友")
    ok_pairs = min(addfriend_ok, newfriends_ok)
    print("    [进度] 删友加好友完成: 成功 %d 对，失败 %d 对，共 %d 对" % (ok_pairs, len(pairs) - ok_pairs, len(pairs)))
    print("    [进度] 开始删除好友 (%d 对)... [%s]" % (len(pairs), _ts()), flush=True)
    results = []
    lock = threading.Lock()

    def del_one(acc_s, acc_r, idx):
        ok = False
        try:
            s, _ = connect(host, port)
            if not s:
                return
            if not login_with_fallback(s, acc_s, timeout=RECV_TIMEOUT_SHORT):
                s.close()
                return
            send_json(s, {"tag": "deletefriend", "account": acc_s, "friend": acc_r})
            r, _ = recv_packet(s, timeout=RECV_TIMEOUT_DEFAULT)
            s.close()
            ok = r and r.get("tag") == "deletefriendsucceed"
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass
        finally:
            with lock:
                results.append((idx, ok))

    t0 = time.perf_counter()
    stop_prog = threading.Event()
    def _del_progress():
        while not stop_prog.wait(2):
            n = len(results)
            if n >= len(pairs):
                break
            ok_cnt = sum(1 for _, ok in results if ok)
            print("    [进度] 删友: 已完成 %d/%d 对, 成功 %d 失败 %d [%s]" % (n, len(pairs), ok_cnt, n - ok_cnt, _ts()), flush=True)
    t_prog = threading.Thread(target=_del_progress, daemon=True)
    t_prog.start()
    conc = DELETEFRIEND.get("concurrent", -1)
    if conc == -1:
        conc = len(pairs)
    for start in range(0, len(pairs), conc):
        batch = min(conc, len(pairs) - start)
        print("    [进度] 删好友批次 %d 对，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = [threading.Thread(target=del_one, args=(pairs[start + j][0], pairs[start + j][1], start + j)) for j in range(batch)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    stop_prog.set()
    ok_cnt = sum(1 for _, ok in results if ok)
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d/%d 对, 失败 %d 对, 总耗时 %.1f ms" % (ok_cnt, len(pairs), len(pairs) - ok_cnt, elapsed))


def run_friendinfo(host, port, count, concurrent=None, pre_created_accounts=None):
    c = concurrent if concurrent is not None else FRIENDINFO.get("concurrent", 8)
    if pre_created_accounts and len(pre_created_accounts) >= 2:
        num_pairs = len(pre_created_accounts) // 2
        need = 2 * num_pairs
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
    else:
        num_pairs = min(max(1, count), c * 4)
        need = 2 * num_pairs
        pairs = []
    if not (pre_created_accounts and pairs):
        suffix = str(int(time.time() * 1000))[-6:]
        print("    [进度] 创建索友用户对... [%s]" % _ts(), flush=True)
        for i in range(num_pairs):
            if (i + 1) % 50 == 0 or i == 0:
                print("    [进度] 索友用户: 尝试 %d/%d，成功 %d 对 [%s]" % (i + 1, num_pairs, len(pairs), _ts()), flush=True)
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                continue
            send_json(sa, {"tag": "register", "nickname": "索友发%d_%s" % (i + 1, suffix), "gender": "男", "password": "123456",
                       "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            send_json(sb, {"tag": "register", "nickname": "索友收%d_%s" % (i + 1, suffix), "gender": "女", "password": "123456",
                       "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                pairs.append((ra.get("account_number"), rb.get("account_number")))
        if not pairs:
            fail("注册失败")
            return
        if len(pairs) < num_pairs:
            print("    [进度] 索友用户: 计划 %d 对，成功创建 %d 对 [%s]" % (num_pairs, len(pairs), _ts()), flush=True)
    req_per_pair = max(1, count // len(pairs))
    section("批量索要好友信息：计划 %d 对，每对 %d 次，共 %d 次 (并发：%d)" % (
        len(pairs), req_per_pair, len(pairs) * req_per_pair, c))
    if pre_created_accounts and pairs:
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 对 [%s]" % (need, len(pairs), _ts()), flush=True)
    pe = FRIENDINFO.get("progress_every", 50)
    print("    [进度] 索友用户加好友 (并发：%d)... [%s]" % (ADDFRIEND_PAIR_CONCURRENT, _ts()), flush=True)
    addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
        host, port, pairs, ADDFRIEND_PAIR_CONCURRENT, pe, "索友加好友")
    ok_pairs = min(addfriend_ok, newfriends_ok)
    print("    [进度] 索友加好友完成: 成功 %d 对，失败 %d 对，共 %d 对" % (ok_pairs, len(pairs) - ok_pairs, len(pairs)))
    time.sleep(0.3)  # 等待服务端好友关系同步
    print("    [进度] 开始索要好友信息 (%d 对，每对 %d 次，共 %d 次)... [%s]" % (len(pairs), req_per_pair, len(pairs) * req_per_pair, _ts()), flush=True)
    results = []
    lock = threading.Lock()

    def ask_one(acc_s, acc_r, idx, n_req):
        ok_cnt = 0
        try:
            s, _ = connect(host, port)
            if not s:
                return
            if not login_with_fallback(s, acc_s, timeout=RECV_TIMEOUT_SHORT):
                s.close()
                return
            for _ in range(n_req):
                for attempt in range(2):
                    send_json(s, {"tag": "askforfriendinfor", "account": acc_s, "friend": acc_r})
                    r = None
                    for _i in range(5):
                        resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_RELAXED)
                        if not resp or not isinstance(resp, dict):
                            continue
                        if resp.get("tag") == "friendinfor":
                            r = resp
                            break
                    if r and r.get("tag") == "friendinfor" and not r.get("error"):
                        ok_cnt += 1
                        break
                    if attempt == 0 and r and r.get("error"):
                        time.sleep(0.2)  # 重试：可能为好友关系尚未完全同步
            s.close()
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass
        finally:
            with lock:
                results.append((idx, ok_cnt, n_req))

    t0 = time.perf_counter()
    stop_fi = threading.Event()

    def _fi_progress():
        last_n = -1
        while not stop_fi.wait(2):
            n = len(results)
            if n >= len(pairs):
                break
            if n > last_n:
                last_n = n
                total_ok = sum(r[1] for r in results)
                total_req = sum(r[2] for r in results)
                print("    [进度] 索友: 已完成 %d/%d 对, 成功 %d 次 失败 %d 次 [%s]" % (n, len(pairs), total_ok, total_req - total_ok, _ts()), flush=True)

    fi_prog = threading.Thread(target=_fi_progress, daemon=True)
    fi_prog.start()

    for start in range(0, len(pairs), c):
        batch = min(c, len(pairs) - start)
        print("    [进度] 索要资料批次 %d 对，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = []
        for j in range(batch):
            i = start + j
            acc_s, acc_r = pairs[i]
            t = threading.Thread(target=ask_one, args=(acc_s, acc_r, i, req_per_pair))
            threads.append(t)
            t.start()
        for t in threads:
            t.join()
    stop_fi.set()
    total_ok = sum(r[1] for r in results)
    total_req = sum(r[2] for r in results)
    ms = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d/%d 次, 失败 %d 次, %d 对（计划 %d 对）, 总耗时 %.1f ms" % (total_ok, total_req, total_req - total_ok, len(pairs), num_pairs, ms))


def run_document(host, port, count, pre_created_accounts=None):
    """文件上传下载（多线程并发）：每人发1收1，count 对，使用 res 目录下文件"""
    doc_conc = DOCUMENT.get("concurrent", 8)
    section("批量文件：计划 %d 对 (并发：%d)" % (count, doc_conc))
    file_content = get_document_content()
    if not file_content:
        fail("无文件内容 (res/example1.jpg)")
        return
    pairs = []
    need = 2 * count
    if pre_created_accounts and len(pre_created_accounts) >= need:
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 对 [%s]" % (need, len(pairs), _ts()), flush=True)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        doc_reg_ok = 0
        print("    [进度] 创建文件用户对... [%s]" % _ts(), flush=True)
        for i in range(count):
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                if (i + 1) % 10 == 0 or i == 0:
                    print("    [进度] 文件用户: 已创建 %d/%d 对, 成功 %d 失败 %d [%s]" % (i + 1, count, doc_reg_ok, (i + 1) - doc_reg_ok, _ts()), flush=True)
                continue
            send_json(sa, {"tag": "register", "nickname": "文件发%d_%s" % (i + 1, suffix), "gender": "男",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=RECV_TIMEOUT_INSTANT)
            send_json(sb, {"tag": "register", "nickname": "文件收%d_%s" % (i + 1, suffix), "gender": "女",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=RECV_TIMEOUT_INSTANT)
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                pairs.append((ra.get("account_number"), rb.get("account_number")))
                doc_reg_ok += 1
            if (i + 1) % 10 == 0 or i == 0:
                print("    [进度] 文件用户: 已创建 %d/%d 对, 成功 %d 失败 %d [%s]" % (i + 1, count, doc_reg_ok, (i + 1) - doc_reg_ok, _ts()), flush=True)
        print("    [进度] 文件用户创建完成: 成功 %d 对 失败 %d 对" % (doc_reg_ok, count - doc_reg_ok))
    pe = DOCUMENT.get("progress_every", 10)
    print("    [进度] 文件用户加好友 (并发：%d)... [%s]" % (ADDFRIEND_PAIR_CONCURRENT, _ts()), flush=True)
    addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
        host, port, pairs, ADDFRIEND_PAIR_CONCURRENT, pe, "文件加好友")
    ok_pairs = min(addfriend_ok, newfriends_ok)
    print("    [进度] 文件加好友完成: 成功 %d 对，失败 %d 对，共 %d 对" % (ok_pairs, len(pairs) - ok_pairs, len(pairs)))
    print("    [进度] 开始文件上传下载 (%d 对)... [%s]" % (len(pairs), _ts()), flush=True)
    results = []
    lock = threading.Lock()
    doc_first_fail = [None]

    def doc_pair_one(acc_sender, acc_receiver, idx):
        upload_ok, download_ok = False, False
        fail_reason = None
        try:
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if doc_first_fail[0] is None:
                    doc_first_fail[0] = "连接失败"
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                with lock:
                    results.append((idx, False, False))
                return
            send_json(sa, {"tag": "login", "account_number": acc_sender, "password": "123456"})
            recv_packet(sa, timeout=RECV_TIMEOUT_INSTANT)
            send_json(sa, {"tag": "addfriend", "account": acc_sender, "friend": acc_receiver})
            r_add, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            send_json(sb, {"tag": "login", "account_number": acc_receiver, "password": "123456"})
            recv_packet(sb, timeout=RECV_TIMEOUT_INSTANT)
            send_json(sb, {"tag": "newfriends", "account": acc_receiver, "sender": acc_sender, "answer": "accept"})
            recv_packet(sb, timeout=RECV_TIMEOUT_SHORT)
            file_uuid = str(uuid.uuid4())
            content = file_content
            fname = get_document_filename()
            send_json(sa, {"tag": "upload_begin", "uuid": file_uuid, "sender": acc_sender, "receiver": acc_receiver,
                          "filename": fname, "messagetype": "document",
                          "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(len(content))})
            resp, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            for _ in range(10):  # 若收到登录等遗留包，继续读直到 upload_begin_ack
                if resp is None or not isinstance(resp, dict) or resp.get("tag") in ("upload_begin_ack", "upload_error"):
                    break
                resp, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            if resp and resp.get("tag") == "upload_begin_ack":
                chunk_size = 64 * 1024
                chunk_interval = DOCUMENT.get("chunk_interval_sec", 0.03)
                for j in range(0, len(content), chunk_size):
                    chunk = content[j:j + chunk_size]
                    send_doc_chunk(sa, file_uuid, j // chunk_size, chunk)
                    if chunk_interval > 0 and j + chunk_size < len(content):
                        time.sleep(chunk_interval)
                send_json(sa, {"tag": "upload_end", "uuid": file_uuid})
                for _ in range(UPLOAD_DONE_MAX_LOOPS):
                    resp, _ = recv_packet(sa, timeout=UPLOAD_DONE_TIMEOUT)
                    if isinstance(resp, dict):
                        tag = resp.get("tag", "")
                        if tag == "uploaddone":
                            upload_ok = True
                            break
                        if tag == "upload_error":
                            fail_reason = "upload_error: %s" % resp.get("message", "未知")
                            break
                if not upload_ok and fail_reason is None:
                    fail_reason = "未收到 uploaddone"
            else:
                if resp and resp.get("tag") == "upload_error":
                    fail_reason = "upload_begin 失败: %s" % resp.get("message", "未知")
                elif resp is None:
                    fail_reason = "未收到 upload_begin_ack（超时）"
                else:
                    fail_reason = "未收到 upload_begin_ack（tag=%s）" % resp.get("tag", "?")
            sa.close()
            got_begin = got_end = False
            time.sleep(0.5)  # 等待服务端 upload_end 完成 DB 写入，避免 askfordocument 时 DB 中无记录
            send_json(sb, {"tag": "askfordocument", "uuid": file_uuid, "account": acc_receiver})
            for _ in range(90):
                resp, ptype = recv_packet(sb, timeout=RECV_TIMEOUT_DEFAULT)  # 文档泵送可能较慢，用默认 45s
                if resp is None:
                    if fail_reason is None and not (got_begin and got_end):
                        fail_reason = ("收包超时或连接关闭（已收到 document_begin 后未收到 document_end）"
                                       if got_begin else "收包超时或连接关闭（未收到 document_begin）")
                    break
                if ptype == PAYLOAD_JSON and isinstance(resp, dict):
                    tag = resp.get("tag", "")
                    if tag == "document_begin":
                        got_begin = True
                    elif tag == "document_end":
                        got_end = True
                        break
                    elif tag == "document_error":
                        if fail_reason is None:
                            fail_reason = "document_error: %s" % resp.get("message", "未知")
                        break
            sb.close()
            download_ok = got_begin and got_end
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError) as e:
            fail_reason = str(type(e).__name__)
        finally:
            if fail_reason and doc_first_fail[0] is None:
                doc_first_fail[0] = fail_reason
            with lock:
                results.append((idx, upload_ok, download_ok))

    t0 = time.perf_counter()
    for start in range(0, len(pairs), doc_conc):
        batch = min(doc_conc, len(pairs) - start)
        print("    [进度] 文件批次 %d 对，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = [threading.Thread(target=doc_pair_one, args=(pairs[start + j][0], pairs[start + j][1], start + j))
                  for j in range(batch)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        done = min(start + batch, len(pairs))
        if done < len(pairs):
            up_ok = sum(1 for _, u, _ in results if u)
            dl_ok = sum(1 for _, _, d in results if d)
            print("    [进度] 文件: 已完成 %d/%d 对, 上传成功 %d 下载成功 %d [%s]" % (done, len(pairs), up_ok, dl_ok, _ts()), flush=True)
    doc_upload_ok = sum(1 for _, u, _ in results if u)
    doc_download_ok = sum(1 for _, _, d in results if d)
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: %d 对，上传成功 %d 失败 %d，下载成功 %d 失败 %d，总耗时 %.1f ms" %
          (len(pairs), doc_upload_ok, len(pairs) - doc_upload_ok, doc_download_ok, len(pairs) - doc_download_ok, elapsed))
    if (doc_upload_ok < len(pairs) or doc_download_ok < len(pairs)) and doc_first_fail[0]:
        print("    [诊断] 首个失败: %s" % doc_first_fail[0])


def run_clouddrive(host, port, count, pre_created_accounts=None):
    """速聊网盘：上传者 cloud 上传 → list/search → 另一账号凭 file_id 下载（无需好友）"""
    cloud_conc = CLOUDDRIVE.get("concurrent", 2)
    section("速聊网盘：计划 %d 组 (并发：%d)" % (count, cloud_conc))
    file_content = get_document_content()
    if not file_content:
        fail("无文件内容 (res/example1.jpg)")
        return
    pairs = []
    need = 2 * count
    if pre_created_accounts and len(pre_created_accounts) >= need:
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 组 [%s]" % (need, len(pairs), _ts()), flush=True)
    else:
        suffix = str(int(time.time() * 1000))[-6:]
        cloud_reg_ok = 0
        print("    [进度] 创建网盘用户组... [%s]" % _ts(), flush=True)
        for i in range(count):
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                if (i + 1) % 10 == 0 or i == 0:
                    print("    [进度] 网盘用户: 已创建 %d/%d 组, 成功 %d [%s]" % (
                        i + 1, count, cloud_reg_ok, _ts()), flush=True)
                continue
            send_json(sa, {"tag": "register", "nickname": "网盘传%d_%s" % (i + 1, suffix), "gender": "男",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=RECV_TIMEOUT_INSTANT)
            send_json(sb, {"tag": "register", "nickname": "网盘下%d_%s" % (i + 1, suffix), "gender": "女",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=RECV_TIMEOUT_INSTANT)
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                pairs.append((ra.get("account_number"), rb.get("account_number")))
                cloud_reg_ok += 1
            if (i + 1) % 10 == 0 or i == 0:
                print("    [进度] 网盘用户: 已创建 %d/%d 组, 成功 %d [%s]" % (
                    i + 1, count, cloud_reg_ok, _ts()), flush=True)
        print("    [进度] 网盘用户创建完成: 成功 %d 组 失败 %d 组" % (cloud_reg_ok, count - cloud_reg_ok))
    if not pairs:
        fail("无可用网盘测试账号组")
        return
    pe = CLOUDDRIVE.get("progress_every", 5)
    print("    [进度] 开始网盘上传/查询/下载 (%d 组)... [%s]" % (len(pairs), _ts()), flush=True)
    results = []
    lock = threading.Lock()
    cloud_first_fail = [None]

    def cloud_pair_one(acc_uploader, acc_downloader, idx):
        upload_ok = list_ok = search_ok = download_ok = False
        fail_reason = None
        file_uuid = str(uuid.uuid4())
        resolved_file_id = file_uuid
        acc_up = normalize_account_key(acc_uploader)
        acc_dl = normalize_account_key(acc_downloader)
        fname = get_document_filename()
        content = file_content
        try:
            sa, _ = connect(host, port)
            if not sa:
                if cloud_first_fail[0] is None:
                    cloud_first_fail[0] = "上传连接失败"
                with lock:
                    results.append((idx, False, False, False, False))
                return
            send_json(sa, {"tag": "upload_begin", "uuid": file_uuid, "sender": acc_up, "receiver": acc_up,
                          "filename": fname, "messagetype": "document",
                          "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(len(content)),
                          "cloud": True})
            resp, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            for _ in range(10):
                if resp is None or not isinstance(resp, dict) or resp.get("tag") in ("upload_begin_ack", "upload_error"):
                    break
                resp, _ = recv_packet(sa, timeout=RECV_TIMEOUT_SHORT)
            if resp and resp.get("tag") == "upload_begin_ack":
                chunk_size = 64 * 1024
                chunk_interval = CLOUDDRIVE.get("chunk_interval_sec", 0.03)
                for j in range(0, len(content), chunk_size):
                    chunk = content[j:j + chunk_size]
                    send_doc_chunk(sa, file_uuid, j // chunk_size, chunk)
                    if chunk_interval > 0 and j + chunk_size < len(content):
                        time.sleep(chunk_interval)
                send_json(sa, {"tag": "upload_end", "uuid": file_uuid})
                got_cloud_done = False
                for _ in range(UPLOAD_DONE_MAX_LOOPS):
                    resp, _ = recv_packet(sa, timeout=UPLOAD_DONE_TIMEOUT)
                    if not isinstance(resp, dict):
                        continue
                    tag = resp.get("tag", "")
                    if tag == "cloud_upload_done":
                        got_cloud_done = True
                        resolved_file_id = resp.get("file_id", file_uuid)
                        upload_ok = True
                    elif tag == "uploaddone":
                        if got_cloud_done or upload_ok:
                            upload_ok = True
                            break
                        upload_ok = True
                        break
                    elif tag == "upload_error":
                        fail_reason = "upload_error: %s" % resp.get("message", "未知")
                        break
                if not upload_ok and fail_reason is None:
                    fail_reason = "未收到 cloud_upload_done/uploaddone"
            else:
                if resp and resp.get("tag") == "upload_error":
                    fail_reason = "upload_begin 失败: %s" % resp.get("message", "未知")
                elif resp is None:
                    fail_reason = "未收到 upload_begin_ack（超时）"
                else:
                    fail_reason = "未收到 upload_begin_ack（tag=%s）" % resp.get("tag", "?")
            sa.close()

            time.sleep(0.5)

            sm, _ = connect(host, port)
            if sm and login_with_fallback(sm, acc_up):
                send_json(sm, {"tag": "listmycloudfiles", "account": acc_up})
                for _ in range(30):
                    resp, _ = recv_packet(sm, timeout=RECV_TIMEOUT_STANDARD)
                    if not isinstance(resp, dict):
                        continue
                    if resp.get("tag") != "listmycloudfiles_result":
                        continue
                    if resp.get("ok") == "true":
                        files = resp.get("files", [])
                        for item in files:
                            if not isinstance(item, dict):
                                continue
                            fid = item.get("file_id", "")
                            if fid == resolved_file_id or fid == file_uuid:
                                list_ok = True
                                break
                    break
            if sm:
                sm.close()

            ss, _ = connect(host, port)
            if ss and login_with_fallback(ss, acc_dl):
                send_json(ss, {"tag": "searchcloudfile", "file_id": resolved_file_id, "account": acc_dl})
                for _ in range(30):
                    resp, _ = recv_packet(ss, timeout=RECV_TIMEOUT_STANDARD)
                    if not isinstance(resp, dict):
                        continue
                    if resp.get("tag") != "searchcloudfile_result":
                        continue
                    if resp.get("found") == "true" and resp.get("file_id", resolved_file_id) == resolved_file_id:
                        search_ok = True
                    break
            if ss:
                ss.close()

            sb, _ = connect(host, port)
            if not sb:
                if fail_reason is None:
                    fail_reason = "下载连接失败"
            else:
                send_json(sb, {"tag": "askforcloudfile", "file_id": resolved_file_id, "account": acc_dl})
                got_begin = got_end = False
                for _ in range(90):
                    resp, ptype = recv_packet(sb, timeout=RECV_TIMEOUT_DEFAULT)
                    if resp is None:
                        if fail_reason is None and not (got_begin and got_end):
                            fail_reason = ("收包超时（已收到 document_begin 后未收到 document_end）"
                                           if got_begin else "收包超时（未收到 document_begin）")
                        break
                    if ptype == PAYLOAD_JSON and isinstance(resp, dict):
                        tag = resp.get("tag", "")
                        if tag == "document_begin":
                            got_begin = True
                        elif tag == "document_end":
                            got_end = True
                            break
                        elif tag == "document_error":
                            if fail_reason is None:
                                fail_reason = "document_error: %s" % resp.get("message", "未知")
                            break
                sb.close()
                download_ok = got_begin and got_end
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError) as e:
            fail_reason = str(type(e).__name__)
        finally:
            if fail_reason and cloud_first_fail[0] is None:
                cloud_first_fail[0] = fail_reason
            with lock:
                results.append((idx, upload_ok, list_ok, search_ok, download_ok))

    t0 = time.perf_counter()
    for start in range(0, len(pairs), cloud_conc):
        batch = min(cloud_conc, len(pairs) - start)
        print("    [进度] 网盘批次 %d 组，起始 %d [%s]" % (batch, start, _ts()), flush=True)
        threads = [threading.Thread(target=cloud_pair_one,
                                    args=(pairs[start + j][0], pairs[start + j][1], start + j))
                   for j in range(batch)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        done = min(start + batch, len(pairs))
        if done < len(pairs) and pe:
            up_ok = sum(1 for _, u, _, _, _ in results if u)
            dl_ok = sum(1 for _, _, _, _, d in results if d)
            print("    [进度] 网盘: 已完成 %d/%d 组, 上传 %d 下载 %d [%s]" % (
                done, len(pairs), up_ok, dl_ok, _ts()), flush=True)
    up_ok = sum(1 for _, u, _, _, _ in results if u)
    list_ok_n = sum(1 for _, _, l, _, _ in results if l)
    search_ok_n = sum(1 for _, _, _, s, _ in results if s)
    dl_ok = sum(1 for _, _, _, _, d in results if d)
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: %d 组，上传 %d/%d，列表 %d/%d，查询 %d/%d，下载 %d/%d，总耗时 %.1f ms" %
          (len(pairs), up_ok, len(pairs), list_ok_n, len(pairs), search_ok_n, len(pairs), dl_ok, len(pairs), elapsed))
    if cloud_first_fail[0] and (up_ok < len(pairs) or dl_ok < len(pairs)):
        print("    [诊断] 首个失败: %s" % cloud_first_fail[0])


def run_messageread(host, port, count, concurrent=None, pre_created_accounts=None):
    c = concurrent if concurrent is not None else MESSAGEREAD.get("concurrent", -1)
    if c == -1:
        c = 9999
    if pre_created_accounts and len(pre_created_accounts) >= 2:
        max_pairs = len(pre_created_accounts) // 2
        num_pairs = min(count, max_pairs)   # 尊重配置 pairs，不超过可用账号
        need = 2 * num_pairs
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
    else:
        num_pairs = min(max(1, count), c * 4)   # count 即配置的 pairs
        need = 2 * num_pairs
        pairs = []
    if not (pre_created_accounts and pairs):
        suffix = str(int(time.time() * 1000))[-6:]
        print("    [进度] 创建已读用户对... [%s]" % _ts(), flush=True)
        for i in range(num_pairs):
            if (i + 1) % 20 == 0 or i == 0:
                print("    [进度] 已读用户: 尝试 %d/%d，成功 %d 对 [%s]" % (i + 1, num_pairs, len(pairs), _ts()), flush=True)
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                continue
            send_json(sa, {"tag": "register", "nickname": "已读发%d_%s" % (i + 1, suffix), "gender": "男", "password": "123456",
                       "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            send_json(sb, {"tag": "register", "nickname": "已读收%d_%s" % (i + 1, suffix), "gender": "女", "password": "123456",
                       "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=REG.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                pairs.append((ra.get("account_number"), rb.get("account_number")))
        if not pairs:
            fail("注册失败")
            return
        if len(pairs) < num_pairs:
            print("    [进度] 已读用户: 计划 %d 对，成功创建 %d 对 [%s]" % (num_pairs, len(pairs), _ts()), flush=True)
    read_per_pair = max(1, count // len(pairs))
    cfg_mr_c = concurrent if concurrent is not None else MESSAGEREAD.get("concurrent", -1)
    section("批量消息已读：计划 %d 对，每对 %d 条，共 %d 条 %s" % (
        len(pairs), read_per_pair, len(pairs) * read_per_pair, conc_paren(cfg_mr_c, len(pairs))))
    if pre_created_accounts and pairs:
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 对 [%s]" % (need, len(pairs), _ts()), flush=True)
    pe = MESSAGEREAD.get("progress_every", 20)
    print("    [进度] 已读用户加好友 (并发：%d)... [%s]" % (ADDFRIEND_PAIR_CONCURRENT, _ts()), flush=True)
    addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
        host, port, pairs, ADDFRIEND_PAIR_CONCURRENT, pe, "已读加好友")
    ok_pairs = min(addfriend_ok, newfriends_ok)
    print("    [进度] 已读加好友完成: 成功 %d 对，失败 %d 对，共 %d 对" % (ok_pairs, len(pairs) - ok_pairs, len(pairs)))
    print("    [进度] 开始消息已读 (%d 对 %s)... [%s]" % (len(pairs), conc_paren(cfg_mr_c, len(pairs)), _ts()), flush=True)
    results = []
    lock = threading.Lock()

    def read_pair_one(acc_s, acc_r, idx, n_read):
        ok_cnt = 0
        try:
            s_send, _ = connect(host, port)
            if not s_send:
                return
            send_json(s_send, {"tag": "login", "account_number": acc_s, "password": "123456"})
            recv_packet(s_send, timeout=RECV_TIMEOUT_SHORT)
            msg_uuids = []
            for i in range(n_read):
                msg_uuid = str(uuid.uuid4())
                send_json(s_send, {"tag": "messages", "sender": acc_s, "receiver": acc_r, "messagetype": "text",
                           "uuid": msg_uuid, "messages": "已读测试_%d_%d" % (idx + 1, i + 1), "filename": ""})
                recv_packet(s_send, timeout=RECV_TIMEOUT_SHORT)
                msg_uuids.append(msg_uuid)
                if i < n_read - 1:
                    time.sleep(0.03)  # 降低并发，避免服务端消息任务队列积压
            s_send.close()
            time.sleep(0.3)  # 等待服务端消息入库
            s_recv, _ = connect(host, port)
            if not s_recv:
                return
            send_json(s_recv, {"tag": "login", "account_number": acc_r, "password": "123456"})
            recv_packet(s_recv, timeout=RECV_TIMEOUT_SHORT)
            for msg_uuid in msg_uuids:
                send_json(s_recv, {"tag": "messageread", "uuid": msg_uuid, "account": acc_r})
                resp, _ = recv_packet(s_recv, timeout=RECV_TIMEOUT_SHORT)
                if resp and resp.get("tag") == "messageread_ack":
                    ok_cnt += 1
            s_recv.close()
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass  # 连接被服务端关闭等，已完成的 ok_cnt 计入结果
        finally:
            with lock:
                results.append((idx, ok_cnt, n_read))

    t0 = time.perf_counter()
    stop_mr_progress = threading.Event()

    def _mr_progress_loop():
        last_n = -1
        while not stop_mr_progress.wait(2):
            n = len(results)
            if n >= len(pairs):
                break
            if n > last_n:
                last_n = n
                total_ok = sum(r[1] for r in results)
                total_read = sum(r[2] for r in results)
                print("    [进度] 已读: 已完成 %d/%d 对, 成功 %d 条 失败 %d 条 [%s]" % (n, len(pairs), total_ok, total_read - total_ok, _ts()), flush=True)

    mr_progress_thread = threading.Thread(target=_mr_progress_loop, daemon=True)
    mr_progress_thread.start()

    threads = []
    batch_start = 8  # 分批启动线程，降低服务端瞬时压力（与标题中并发数一致：每对一线程）
    for i, (acc_s, acc_r) in enumerate(pairs):
        t = threading.Thread(target=read_pair_one, args=(acc_s, acc_r, i, read_per_pair))
        threads.append(t)
        t.start()
        if (i + 1) % batch_start == 0 and i + 1 < len(pairs):
            time.sleep(0.05)
    for t in threads:
        t.join()
    stop_mr_progress.set()
    total_ok = sum(r[1] for r in results)
    total_read = sum(r[2] for r in results)
    expected_total = len(pairs) * read_per_pair
    ms = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d 条, 失败 %d 条, 共 %d 条（预期 %d）, %d 对, 总耗时 %.1f ms" % (
        total_ok, total_read - total_ok, total_read, expected_total, len(pairs), ms))


def run_batch(host, port, count):
    batch = BATCH.get("batch", 100)
    section("批量心跳：%d 次 (并发：%d)" % (count, batch))
    results = []
    lock = threading.Lock()

    def heart_one(i):
        ok, ms = False, 0
        try:
            s, _ = connect(host, port)
            if not s:
                return
            t0 = time.perf_counter()
            send_json(s, {"tag": "heart"})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            ms = (time.perf_counter() - t0) * 1000
            s.close()
            ok = resp and resp.get("tag") == "pong"
        except (ConnectionResetError, ConnectionError, OSError, BrokenPipeError):
            pass
        finally:
            with lock:
                results.append((i, ok, ms))

    t0 = time.perf_counter()
    stop_prog = threading.Event()
    def _heart_progress():
        while not stop_prog.wait(2):
            n = len(results)
            if n >= count:
                break
            ok_cnt = sum(1 for _, ok, _ in results if ok)
            print("    [进度] 心跳: 已完成 %d/%d, 成功 %d 失败 %d [%s]" % (n, count, ok_cnt, n - ok_cnt, _ts()), flush=True)
    t_prog = threading.Thread(target=_heart_progress, daemon=True)
    t_prog.start()
    for start in range(0, count, batch):
        b = min(batch, count - start)
        print("    [进度] 心跳批次 %d 条，起始 %d [%s]" % (b, start, _ts()), flush=True)
        threads = [threading.Thread(target=heart_one, args=(start + j,)) for j in range(b)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    stop_prog.set()
    ok_cnt = sum(1 for _, ok, _ in results if ok)
    latencies = [ms for _, ok, ms in results if ok and ms > 0]
    elapsed = (time.perf_counter() - t0) * 1000
    print("-" * 50)
    print("  结果: 成功 %d, 失败 %d, 总耗时 %.1f ms" % (ok_cnt, count - ok_cnt, elapsed))
    if latencies:
        print("  延迟: 最小 %.1f ms, 最大 %.1f ms, 平均 %.1f ms" %
              (min(latencies), max(latencies), sum(latencies) / len(latencies)))


class _Tee:
    """同时写入多个流"""
    def __init__(self, *streams):
        self._streams = streams

    def write(self, data):
        for s in self._streams:
            s.write(data)
            s.flush()

    def flush(self):
        for s in self._streams:
            s.flush()


def run_stress(host, port, conn_count=None, duration_sec=None):
    conn_count = conn_count if conn_count is not None else STRESS.get("conn", 300)
    duration_sec = duration_sec if duration_sec is not None else STRESS.get("duration", 60)
    section("压测：%d 秒 (并发：%d)" % (duration_sec, conn_count))
    socks = []
    print("    [进度] 建立连接... [%s]" % _ts(), flush=True)
    for i in range(conn_count):
        if (i + 1) % 200 == 0 or i == 0:
            print("    [进度] 连接: 已建立 %d/%d [%s]" % (i + 1, conn_count, _ts()), flush=True)
        s, _ = connect(host, port)
        if s:
            socks.append(s)
    print("  建立 %d 个连接" % len(socks))
    start = time.perf_counter()
    ok_cnt, fail_cnt = 0, 0
    latencies = []
    round_count = 0
    while time.perf_counter() - start < duration_sec:
        round_start = time.perf_counter()
        for s in socks:
            t0 = time.perf_counter()
            send_json(s, {"tag": "heart"})
            resp, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
            ms = (time.perf_counter() - t0) * 1000
            latencies.append(ms)
            if resp and resp.get("tag") == "pong":
                ok_cnt += 1
            else:
                fail_cnt += 1
        round_count += 1
        elapsed = time.perf_counter() - start
        if round_count > 0 and round_count % 30 == 0:
            print("    [进度] 压测: 已运行 %d 秒, 成功 %d 失败 %d [%s]" % (int(elapsed), ok_cnt, fail_cnt, _ts()), flush=True)
        if elapsed >= duration_sec:
            break
        time.sleep(max(0, 1 - (time.perf_counter() - round_start)))
    for s in socks:
        try:
            s.close()
        except Exception:
            pass
    total_time = time.perf_counter() - start
    print("-" * 50)
    print("  结果: 心跳成功 %d, 失败 %d" % (ok_cnt, fail_cnt))
    print("  总耗时: %.2f 秒" % total_time)
    print("  QPS: %.1f 次/秒" % (ok_cnt / total_time if total_time > 0 else 0))
    if latencies:
        print("  延迟: 最小 %.1f ms, 最大 %.1f ms, 平均 %.1f ms" %
              (min(latencies), max(latencies), sum(latencies) / len(latencies)))


def run_mixed(host, port, duration_sec=None, workers_cfg=None, pre_created_accounts=None):
    duration = duration_sec if duration_sec is not None else MIXED.get("duration", 60)
    workers = workers_cfg if workers_cfg is not None else MIXED.get("workers", {})
    _wk = ("reg", "login", "heart", "msg", "doc", "upload", "friendinfo", "messageread", "changeinfo", "findpwd")
    _w_sum = sum(int(workers.get(k, 0)) for k in _wk)
    section("混合压测：%d 秒（worker 线程合计 %d）" % (duration, _w_sum))
    _w_detail = ", ".join("%s=%d" % (k, int(workers.get(k, 0))) for k in _wk if int(workers.get(k, 0)) > 0)
    if _w_detail:
        print("    [说明] 合计 %d = 各类型 worker 数量相加（非 TCP 连接数）：%s" % (_w_sum, _w_detail), flush=True)
    suffix = str(int(time.time() * 1000))[-6:]
    small_size = 64 * 1024
    size_mb = FULL.get("upload_size_mb", 2)
    size_bytes = int(size_mb * 1024 * 1024)

    pairs = []
    all_accs = []
    if pre_created_accounts and len(pre_created_accounts) >= 2:
        num_pairs = min(MIXED.get("pairs", 30), len(pre_created_accounts) // 2)
        need = 2 * num_pairs
        pairs = [(pre_created_accounts[i], pre_created_accounts[i + 1]) for i in range(0, need, 2)]
        all_accs = list(pre_created_accounts[:need])
        print("    [复用] 使用 reg 阶段 %d 个账号，组成 %d 对 [%s]" % (need, len(pairs), _ts()), flush=True)
        pe = MIXED.get("progress_every", 10)
        _afc = MIXED.get("addfriend_concurrent", ADDFRIEND_PAIR_CONCURRENT)
        print("    [进度] 混合用户加好友 (并发：%d)... [%s]" % (_afc, _ts()), flush=True)
        addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
            host, port, pairs, _afc, pe, "混合加好友")
        ok_pairs = min(addfriend_ok, newfriends_ok)
        print("  已复用 %d 对用户，加好友成功 %d 对，失败 %d 对" % (len(pairs), ok_pairs, len(pairs) - ok_pairs))
    else:
        mixed_pairs = MIXED.get("pairs", 30)
        print("  预创建 %d 对用户..." % mixed_pairs)
        for i in range(mixed_pairs):
            if (i + 1) % 10 == 0 or i == 0:
                print("    [进度] 混合用户: 已创建 %d/%d 对 [%s]" % (i + 1, mixed_pairs, _ts()), flush=True)
            sa, _ = connect(host, port)
            sb, _ = connect(host, port)
            if not sa or not sb:
                if sa:
                    sa.close()
                if sb:
                    sb.close()
                continue
            send_json(sa, {"tag": "register", "nickname": "混合发%d_%s" % (i + 1, suffix), "gender": "男",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            ra, _ = recv_packet(sa, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            send_json(sb, {"tag": "register", "nickname": "混合收%d_%s" % (i + 1, suffix), "gender": "女",
                           "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
            rb, _ = recv_packet(sb, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
            sa.close()
            sb.close()
            if ra and ra.get("answer") == "regissucceed" and rb and rb.get("answer") == "regissucceed":
                acc_s, acc_r = ra.get("account_number"), rb.get("account_number")
                pairs.append((acc_s, acc_r))
                all_accs.extend([acc_s, acc_r])
        pe = MIXED.get("progress_every", 10)
        _afc = MIXED.get("addfriend_concurrent", ADDFRIEND_PAIR_CONCURRENT)
        print("    [进度] 混合用户加好友 (并发：%d)... [%s]" % (_afc, _ts()), flush=True)
        addfriend_ok, newfriends_ok = add_friend_pairs_parallel(
            host, port, pairs, _afc, pe, "混合加好友")
        ok_pairs = min(addfriend_ok, newfriends_ok)
        print("  已创建 %d 对用户，加好友成功 %d 对，失败 %d 对" % (len(pairs), ok_pairs, len(pairs) - ok_pairs))

    if not pairs or not all_accs:
        fail("预创建用户失败")
        return

    # 统计（成功 + 失败）
    stats = {"reg": 0, "login": 0, "heart": 0, "msg": 0, "doc": 0, "upload": 0,
             "friendinfo": 0, "messageread": 0, "changeinfo": 0, "findpwd": 0}
    stats_failed = {"reg": 0, "login": 0, "heart": 0, "msg": 0, "doc": 0, "upload": 0,
                    "friendinfo": 0, "messageread": 0, "changeinfo": 0, "findpwd": 0}
    stats_lock = threading.Lock()
    stop_flag = threading.Event()
    doc_first_fail = [None]  # 首个 doc 失败原因，便于诊断

    def worker_reg():
        cnt = 0
        while not stop_flag.is_set():
            try:
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["reg"] += 1
                    continue
                send_json(s, {"tag": "register", "nickname": "混压_%d_%s" % (cnt, suffix[-4:]), "gender": "男",
                             "password": "123456", "question": "q", "answer": "a", "avator": AVATAR_BASE64})
                r, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                s.close()
                if r and r.get("answer") == "regissucceed":
                    with stats_lock:
                        stats["reg"] += 1
                    cnt += 1
                else:
                    with stats_lock:
                        stats_failed["reg"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["reg"] += 1

    def worker_login():
        while not stop_flag.is_set():
            try:
                acc = random.choice(all_accs)
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["login"] += 1
                    continue
                send_json(s, {"tag": "login", "account_number": acc, "password": "123456"})
                r, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                s.close()
                if r and r.get("answer") == "loginsucceed":
                    with stats_lock:
                        stats["login"] += 1
                else:
                    with stats_lock:
                        stats_failed["login"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["login"] += 1

    def worker_heart():
        while not stop_flag.is_set():
            try:
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["heart"] += 1
                    continue
                send_json(s, {"tag": "heart"})
                r, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
                s.close()
                if r and r.get("tag") == "pong":
                    with stats_lock:
                        stats["heart"] += 1
                else:
                    with stats_lock:
                        stats_failed["heart"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["heart"] += 1

    def worker_msg():
        while not stop_flag.is_set():
            try:
                acc_s, acc_r = random.choice(pairs)
                s, _ = connect(host, port)
                if not s:
                    continue
                send_json(s, {"tag": "login", "account_number": acc_s, "password": "123456"})
                recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                ok_cnt = 0
                for _ in range(MIXED.get("msg_per_loop", 4)):
                    send_json(s, {"tag": "messages", "sender": acc_s, "receiver": acc_r, "messagetype": "text",
                                 "uuid": str(uuid.uuid4()), "messages": "混合消息", "filename": ""})
                    r, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
                    if r and (r.get("tag") in ("ack", "messagehavedone") if isinstance(r, dict) else False or "messages" in str(r)):
                        ok_cnt += 1
                s.close()
                with stats_lock:
                    stats["msg"] += ok_cnt
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                pass

    def worker_doc():
        chunk_size = 64 * 1024
        while not stop_flag.is_set():
            fail_reason = None
            try:
                acc_s, acc_r = random.choice(pairs)
                sa, _ = connect(host, port)
                sb, _ = connect(host, port)
                if not sa or not sb:
                    if sa:
                        sa.close()
                    if sb:
                        sb.close()
                    fail_reason = "连接失败"
                    with stats_lock:
                        stats_failed["doc"] += 1
                        if doc_first_fail[0] is None:
                            doc_first_fail[0] = fail_reason
                    continue
                send_json(sa, {"tag": "login", "account_number": acc_s, "password": "123456"})
                recv_packet(sa, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                file_content = get_document_content()
                if not file_content:
                    sa.close()
                    sb.close()
                    fail_reason = "无文件内容(res/example1.jpg 等)"
                    with stats_lock:
                        stats_failed["doc"] += 1
                        if doc_first_fail[0] is None:
                            doc_first_fail[0] = fail_reason
                    continue
                file_uuid = str(uuid.uuid4())
                send_json(sa, {"tag": "upload_begin", "uuid": file_uuid, "sender": acc_s, "receiver": acc_r,
                              "filename": get_document_filename(),
                              "messagetype": "document",
                              "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(len(file_content))})
                resp, _ = recv_packet(sa, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                for _ in range(10):  # 若收到登录等遗留包，继续读直到 upload_begin_ack
                    if resp is None or not isinstance(resp, dict) or resp.get("tag") in ("upload_begin_ack", "upload_error"):
                        break
                    resp, _ = recv_packet(sa, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                if resp and resp.get("tag") == "upload_begin_ack":
                    for j in range(0, len(file_content), chunk_size):
                        chunk = file_content[j:j + chunk_size]
                        send_doc_chunk(sa, file_uuid, j // chunk_size, chunk)
                        time.sleep(0.01)
                    send_json(sa, {"tag": "upload_end", "uuid": file_uuid})
                    doc_ok = False
                    upload_err_msg = None
                    for _ in range(UPLOAD_DONE_MAX_LOOPS):
                        resp, _ = recv_packet(sa, timeout=UPLOAD_DONE_TIMEOUT)
                        if isinstance(resp, dict) and resp.get("tag") == "uploaddone":
                            with stats_lock:
                                stats["doc"] += 1
                            doc_ok = True
                            break
                        if isinstance(resp, dict) and resp.get("tag") == "upload_error":
                            upload_err_msg = resp.get("message", "未知")
                            fail_reason = "upload_error: %s" % upload_err_msg
                            break
                    if not doc_ok and fail_reason is None:
                        fail_reason = "未收到 uploaddone"
                    if not doc_ok:
                        with stats_lock:
                            stats_failed["doc"] += 1
                            if doc_first_fail[0] is None:
                                doc_first_fail[0] = fail_reason
                else:
                    if resp is None:
                        fail_reason = "未收到 upload_begin_ack（超时/连接关闭）"
                    elif isinstance(resp, dict):
                        tag = resp.get("tag", "?")
                        fail_reason = "未收到 upload_begin_ack（收到 tag=%s）" % tag
                    else:
                        fail_reason = "未收到 upload_begin_ack（收到非预期类型）"
                    with stats_lock:
                        stats_failed["doc"] += 1
                        if doc_first_fail[0] is None:
                            doc_first_fail[0] = fail_reason
                sa.close()
                send_json(sb, {"tag": "login", "account_number": acc_r, "password": "123456"})
                recv_packet(sb, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                time.sleep(0.5)  # 等待服务端 upload 完成 DB 写入
                send_json(sb, {"tag": "askfordocument", "uuid": file_uuid, "account": acc_r})
                for _ in range(30):
                    resp, _ = recv_packet(sb, timeout=RECV_TIMEOUT_SHORT)
                    if resp and isinstance(resp, dict) and resp.get("tag") == "document_end":
                        break
                sb.close()
            except (ConnectionResetError, ConnectionError, OSError, AttributeError) as e:
                with stats_lock:
                    stats_failed["doc"] += 1
                    if doc_first_fail[0] is None:
                        doc_first_fail[0] = "异常: %s" % type(e).__name__

    def worker_upload():
        chunk_size = 64 * 1024
        while not stop_flag.is_set():
            try:
                acc_s, acc_r = random.choice(pairs)
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["upload"] += 1
                    continue
                send_json(s, {"tag": "login", "account_number": acc_s, "password": "123456"})
                recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                file_uuid = str(uuid.uuid4())
                file_content = get_upload_content(size_bytes)
                if not file_content:
                    s.close()
                    with stats_lock:
                        stats_failed["upload"] += 1
                    continue
                send_json(s, {"tag": "upload_begin", "uuid": file_uuid, "sender": acc_s, "receiver": acc_r,
                             "filename": get_document_filename(),
                             "messagetype": "document",
                             "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "total_bytes": str(len(file_content))})
                resp, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                for _ in range(10):  # 若收到登录等遗留包，继续读直到 upload_begin_ack
                    if resp is None or not isinstance(resp, dict) or resp.get("tag") in ("upload_begin_ack", "upload_error"):
                        break
                    resp, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                if resp and resp.get("tag") == "upload_begin_ack":
                    upload_ok = False
                    for i in range(0, len(file_content), chunk_size):
                        chunk = file_content[i:i + chunk_size]
                        send_doc_chunk(s, file_uuid, i // chunk_size, chunk)
                        time.sleep(0.01)
                    send_json(s, {"tag": "upload_end", "uuid": file_uuid})
                    for _ in range(UPLOAD_DONE_MAX_LOOPS):
                        resp, _ = recv_packet(s, timeout=UPLOAD_DONE_TIMEOUT)
                        if isinstance(resp, dict) and resp.get("tag") == "uploaddone":
                            with stats_lock:
                                stats["upload"] += 1
                            upload_ok = True
                            break
                        if resp and resp.get("tag") == "upload_error":
                            break
                    if not upload_ok:
                        with stats_lock:
                            stats_failed["upload"] += 1
                else:
                    with stats_lock:
                        stats_failed["upload"] += 1
                s.close()
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["upload"] += 1

    def worker_friendinfo():
        while not stop_flag.is_set():
            try:
                acc_s, acc_r = random.choice(pairs)
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["friendinfo"] += 1
                    continue
                send_json(s, {"tag": "login", "account_number": acc_s, "password": "123456"})
                recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                send_json(s, {"tag": "askforfriendinfor", "account": acc_s, "friend": acc_r})
                r, _ = recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
                s.close()
                if r and r.get("tag") == "friendinfor" and not r.get("error"):
                    with stats_lock:
                        stats["friendinfo"] += 1
                else:
                    with stats_lock:
                        stats_failed["friendinfo"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["friendinfo"] += 1

    def worker_messageread():
        while not stop_flag.is_set():
            try:
                acc_s, acc_r = random.choice(pairs)
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["messageread"] += 1
                    continue
                send_json(s, {"tag": "login", "account_number": acc_s, "password": "123456"})
                recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                msg_uuid = str(uuid.uuid4())
                send_json(s, {"tag": "messages", "sender": acc_s, "receiver": acc_r, "messagetype": "text",
                             "uuid": msg_uuid, "messages": "已读测试", "filename": ""})
                recv_packet(s, timeout=RECV_TIMEOUT_SHORT)
                s.close()
                s = None
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["messageread"] += 1
                    continue
                send_json(s, {"tag": "login", "account_number": acc_r, "password": "123456"})
                recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                send_json(s, {"tag": "messageread", "uuid": msg_uuid, "account": acc_r})
                recv_packet(s, timeout=RECV_TIMEOUT_SHORT)  # 等待 messageread_ack
                s.close()
                with stats_lock:
                    stats["messageread"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["messageread"] += 1

    def worker_changeinfo():
        while not stop_flag.is_set():
            try:
                acc = random.choice(all_accs)
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["changeinfo"] += 1
                    continue
                send_json(s, {"tag": "login", "account_number": acc, "password": "123456"})
                recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                send_json(s, {"tag": "changeinformation", "account": acc, "nickname": "混压昵称", "gender": "男",
                             "signature": "混压签名", "friendIds": [], "avator": AVATAR_BASE64, "avator_changed": True})
                r, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))  # 含头像上传
                s.close()
                if r and r.get("answer") == "succeed":
                    with stats_lock:
                        stats["changeinfo"] += 1
                else:
                    with stats_lock:
                        stats_failed["changeinfo"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["changeinfo"] += 1

    def worker_findpwd():
        while not stop_flag.is_set():
            try:
                acc = random.choice(all_accs)
                s, _ = connect(host, port)
                if not s:
                    with stats_lock:
                        stats_failed["findpwd"] += 1
                    continue
                send_json(s, {"tag": "findpassword1", "account_number": acc})
                r1, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                if not r1 or r1.get("answer") != "yes":
                    s.close()
                    with stats_lock:
                        stats_failed["findpwd"] += 1
                    continue
                send_json(s, {"tag": "findpassword2", "account_number": acc, "theanswer": "a"})
                r2, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                if not r2 or r2.get("answer") != "yes":
                    s.close()
                    with stats_lock:
                        stats_failed["findpwd"] += 1
                    continue
                send_json(s, {"tag": "findpassword3", "account_number": acc, "theanswer": "a", "password": "123456"})
                r3, _ = recv_packet(s, timeout=MIXED.get("recv_timeout", RECV_TIMEOUT_DEFAULT))
                s.close()
                if r3 and r3.get("answer") == "yes":
                    with stats_lock:
                        stats["findpwd"] += 1
                else:
                    with stats_lock:
                        stats_failed["findpwd"] += 1
            except (ConnectionResetError, ConnectionError, OSError, AttributeError):
                with stats_lock:
                    stats_failed["findpwd"] += 1

    worker_map = {
        "reg": worker_reg, "login": worker_login, "heart": worker_heart,
        "msg": worker_msg, "doc": worker_doc, "upload": worker_upload,
        "friendinfo": worker_friendinfo, "messageread": worker_messageread,
        "changeinfo": worker_changeinfo, "findpwd": worker_findpwd,
    }
    print("    [进度] 混合压测进行中 (%d 秒)... [%s]" % (duration, _ts()), flush=True)
    threads = []
    for name, n in workers.items():
        if n <= 0 or name not in worker_map:
            continue
        for _ in range(n):
            t = threading.Thread(target=worker_map[name])
            threads.append(t)
            t.start()
    mixed_start = time.perf_counter()
    last_print = 0
    while time.perf_counter() - mixed_start < duration:
        time.sleep(10)
        elapsed = int(time.perf_counter() - mixed_start)
        if elapsed >= duration:
            break
        if elapsed > last_print:
            last_print = elapsed
            with stats_lock:
                total = sum(stats.values())
            print("    [进度] 混合压测: 已运行 %d/%d 秒, 总操作 %d (reg=%d login=%d heart=%d msg=%d ...) [%s]" % (
                elapsed, duration, total, stats.get("reg", 0), stats.get("login", 0), stats.get("heart", 0), stats.get("msg", 0), _ts()), flush=True)
    stop_flag.set()
    for t in threads:
        t.join(timeout=THREAD_JOIN_TIMEOUT_SEC)

    print("-" * 50)
    print("  混合压测结果 (%d 秒):" % duration)
    for k in stats:
        v = stats[k]
        f = stats_failed.get(k, 0)
        if f > 0:
            print("    %s: 成功 %d, 失败 %d" % (k, v, f))
        else:
            print("    %s: %d" % (k, v))
    total_ops = sum(stats.values())
    total_fail = sum(stats_failed.values())
    print("  总成功: %d, 总失败: %d, 平均 QPS: %.1f" % (total_ops, total_fail, total_ops / duration if duration > 0 else 0))
    if int(workers.get("doc", 0) or 0) > 0 or int(workers.get("upload", 0) or 0) > 0:
        print("  （已启用 doc/upload：单次耗时长，总 QPS 仅作粗算）")


def main():
    p = argparse.ArgumentParser(
        description="Chat 服务端测试客户端（默认参数见源码 CONFIG 节各字典）",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--host", default=HOST, metavar="ADDR",
                   help="服务器地址（默认 CONFIG.HOST）")
    p.add_argument("--port", type=int, default=PORT, metavar="N",
                   help="服务器端口（默认 CONFIG.PORT）")
    p.add_argument("--mode", choices=["all", "full", "batch", "stress", "mixed", "reg", "msg", "friend",
                   "findpwd", "changeinfo", "deletefriend", "friendinfo", "document", "clouddrive",
                   "messageread"],
                   default="all",
                   help="测试模式：all 顺序执行全部；其余为单项（默认 %(default)s）")
    p.add_argument("--count", type=int, default=None, metavar="N",
                   help="规模：batch=心跳次数；reg=注册数；msg/friend/findpwd/changeinfo=次数；"
                        "deletefriend/friendinfo/document/clouddrive/messageread=对数或组数。"
                        "默认取对应字典的 count 或 pairs")
    p.add_argument("--conn", type=int, default=None, metavar="N",
                   help="仅 stress：并发连接数（默认 STRESS['conn']）")
    p.add_argument("--duration", type=int, default=None, metavar="SEC",
                   help="仅 stress / mixed：持续秒数（默认各字典 duration；all 模式不读取本参数）")
    p.add_argument("--upload-size", type=float, default=None, metavar="MB",
                   help="仅 full：大文件体积累加上限 MB（默认 FULL['upload_size_mb']）")
    p.add_argument("--full-pairs", type=int, default=None, metavar="N",
                   help="仅 full：第一步主流程对数（默认 FULL['pairs']）")
    p.add_argument("--output", "-o", metavar="FILE", default=None,
                   help="将 stdout 同时写入该文件；all 且未指定时自动生成 test_report_时间戳.txt")
    args = p.parse_args()

    if args.mode == "all" and not args.output:
        args.output = "test_report_%s.txt" % time.strftime("%Y%m%d_%H-%M-%S")

    out_file = None
    old_stdout = sys.stdout
    if args.output:
        out_file = open(args.output, "a", encoding="utf-8")
        sys.stdout = _Tee(old_stdout, out_file)

    try:
        _run_main(args)
    finally:
        if out_file:
            sys.stdout = old_stdout
            out_file.close()


def _run_main(args):
    mode_cfg = {"batch": BATCH, "stress": STRESS, "reg": REG, "mixed": MIXED, "msg": MSG, "friend": FRIEND,
                "findpwd": FINDPWD, "changeinfo": CHANGEINFO, "deletefriend": DELETEFRIEND,
                "friendinfo": FRIENDINFO, "document": DOCUMENT, "clouddrive": CLOUDDRIVE,
                "messageread": MESSAGEREAD}
    count = args.count
    if count is None:
        cfg = mode_cfg.get(args.mode, {})
        count = cfg.get("count", cfg.get("pairs", 10))

    if args.mode == "all":
        _run_all(args)
        return

    _print_execution_plan(args)

    if args.mode == "full":
        fp = getattr(args, "full_pairs", None) or FULL.get("pairs", 5)
        run_full_test(args.host, args.port, args.upload_size or FULL.get("upload_size_mb"), FULL.get("msg_per_pair"), FULL.get("upload_pairs"), full_pairs=fp)
    elif args.mode == "batch":
        run_batch(args.host, args.port, count)
    elif args.mode == "stress":
        run_stress(args.host, args.port, args.conn or STRESS.get("conn"), args.duration or STRESS.get("duration"))
    elif args.mode == "mixed":
        _mix_dur = MIXED.get("duration", 50) if args.duration is None else args.duration
        run_mixed(args.host, args.port, _mix_dur)
    elif args.mode == "reg":
        run_batch_register(args.host, args.port, count, concurrent=REG.get("batch"))
    elif args.mode == "msg":
        run_batch_message(args.host, args.port, count)
    elif args.mode == "friend":
        run_batch_friend(args.host, args.port, count)
    elif args.mode == "findpwd":
        run_findpassword(args.host, args.port, count)
    elif args.mode == "changeinfo":
        run_changeinfo(args.host, args.port, count)
    elif args.mode == "deletefriend":
        run_deletefriend(args.host, args.port, count)
    elif args.mode == "friendinfo":
        run_friendinfo(args.host, args.port, count)
    elif args.mode == "document":
        run_document(args.host, args.port, count)
    elif args.mode == "clouddrive":
        run_clouddrive(args.host, args.port, count)
    else:
        run_messageread(args.host, args.port, count)
    print()
    print("  测试结束时间: %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
    print()


def _run_all(args):
    _print_execution_plan(args)
    if args.output:
        print("报告: %s" % args.output)
    accounts = []
    reg_n = REG.get("count", 150)
    modes = [
        ("full", lambda: run_full_test(args.host, args.port, FULL.get("upload_size_mb"), FULL.get("msg_per_pair"), FULL.get("upload_pairs"),
                                        full_pairs=getattr(args, "full_pairs", None) or FULL.get("pairs", 5))),
        ("batch", lambda: run_batch(args.host, args.port, BATCH.get("count", 800))),
        ("stress", lambda: run_stress(args.host, args.port, STRESS.get("conn"), STRESS.get("duration"))),
        ("reg", lambda: run_batch_register(args.host, args.port, reg_n, concurrent=REG.get("batch"))),
        ("mixed", lambda: run_mixed(args.host, args.port, MIXED.get("duration"), pre_created_accounts=accounts)),
        ("msg", lambda: run_batch_message(args.host, args.port, MSG.get("pairs", 75) * MSG.get("msg_per_pair", 5), pre_created_accounts=accounts)),
        ("friend", lambda: run_batch_friend(args.host, args.port, min(reg_n, max(0, len(accounts) - 1)) if accounts else FRIEND.get("count", 149), pre_created_accounts=accounts)),
        ("findpwd", lambda: run_findpassword(args.host, args.port,
            min(FINDPWD.get("count", 75), len(accounts[:reg_n // 2])) if accounts else FINDPWD.get("count", 75),
            pre_created_accounts=accounts[:reg_n // 2] if accounts else None)),
        ("changeinfo", lambda: run_changeinfo(args.host, args.port,
            min(CHANGEINFO.get("count", 75), len(accounts[reg_n // 2:reg_n])) if accounts else CHANGEINFO.get("count", 75),
            pre_created_accounts=accounts[reg_n // 2:reg_n] if accounts else None, concurrent=CHANGEINFO.get("concurrent"))),
        ("deletefriend", lambda: run_deletefriend(args.host, args.port, DELETEFRIEND.get("pairs", 75), pre_created_accounts=accounts)),
        ("friendinfo", lambda: run_friendinfo(args.host, args.port, FRIENDINFO.get("pairs", 75), pre_created_accounts=accounts, concurrent=FRIENDINFO.get("concurrent"))),
        ("document", lambda: run_document(args.host, args.port, DOCUMENT.get("pairs", 8), pre_created_accounts=accounts[:16])),
        ("clouddrive", lambda: run_clouddrive(args.host, args.port, CLOUDDRIVE.get("pairs", 4), pre_created_accounts=accounts[:16])),
        ("messageread", lambda: run_messageread(args.host, args.port, MESSAGEREAD.get("pairs", 75), pre_created_accounts=accounts)),
    ]
    for idx, (name, fn) in enumerate(modes, 1):
        print()
        print("=" * 50)
        print("  [%d/%d] %s  [%s]" % (idx, len(modes), name, _ts()))
        print("=" * 50)
        result = fn()
        if name == "reg" and result is not None:
            accounts[:] = result
    print()
    print("  测试结束时间: %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
    print()


if __name__ == "__main__":
    main()
