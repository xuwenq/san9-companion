"""Intentional UI customizations; original resource evidence stays untouched."""

def extra_controls(dialog_id):
    if dialog_id != 131:
        return []
    return [dict(id=1205, title="演出 1×", window_class={"ordinal": 128},
                 style=0x50010000, exstyle=0, rect=[553, 239, 50, 18],
                 help_id=0, creation_data=b"")]

def control_title(dialog_id, control_id, original):
    if dialog_id == 102 and control_id in (1202, 0xFFFF, 0xFFFFFFFF):
        assert original == "2026年2月9日更新"
        return "版本日期"
    if (dialog_id, control_id) == (133, 1133):
        assert original == "自动同步配置信息"
        return "自动应用都市配置"
    if (dialog_id, control_id) == (131, 1047):
        # 「启用电脑蛮族阵型」->「启用玩家蛮族阵型」。
        # The checkbox no longer drives the original computer-only stub; it now
        # drives our own patch that rewrites the formation attribute table, so
        # the player can pick the barbarian formations too.
        assert original == "启用电脑蛮族阵型", original
        return "启用玩家蛮族阵型"
    return original


def customize_control_layout(dialog_id, control):
    if dialog_id == 131 and control["id"] in (1010, 1004):
        control["rect"] = [553, 183 if control["id"] == 1010 else 211, 50, 18]
    if (dialog_id, control["id"]) in ((133, 1121), (134, 1138), (135, 1149)):
        # List positions are game record IDs; text sorting breaks the row mapping.
        control["style"] &= ~0x0002  # LBS_SORT
    if dialog_id == 102 and control["id"] in (0xFFFF, 0xFFFFFFFF):
        control["id"] = 1202
        control["rect"] = [15, 82, 120, 8]
        control["style"] = (control["style"] & ~0x1F) | 0x01  # SS_CENTER


def dialog_title(dialog_id, original):
    return "三国志9优化伴侣" if dialog_id == 102 else original
