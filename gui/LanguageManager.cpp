#include "LanguageManager.h"
#include "managers/SettingsManager.h"
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QWidget>

namespace {

// English -> Simplified Chinese lookup.
QHash<QString, QString>& zhMap() {
    static QHash<QString, QString> m = {
        {"File", "文件"},
        {"Settings", "设置"},
        {"View", "视图"},
        {"Help", "帮助"},
        {"Exit", "退出"},
        {"Reindex Cache", "重新索引缓存"},
        {"Preferences", "偏好设置"},
        {"Show DSL Editor", "显示 DSL 编辑区"},
        {"Show Scores", "显示概率"},
        {"Refresh", "刷新"},
        {"Reset Layout", "重置布局"},
        {"About", "关于"},
        {"Check for Updates", "检查更新"},
        {"Enter a natural-language description, e.g. a cat and a dog, cat on the left...",
         "输入自然语言描述图片，例如：一只猫和一只狗，猫在左边..."},
        {"Translate to DSL (→)", "翻译为 DSL (→)"},
        {"Search (▶)", "执行检索 (▶)"},
        {"🏷️ Tag Filter", "🏷️ 标签筛选"},
        {"🗑 Delete Selected", "🗑 删除选中"},
        {"Delete the selected images (disk files + cache)", "删除选中的图片（磁盘文件 + 缓存）"},
        {"Filter Options", "筛选选项"},
        {"Pre-filter images by tags (DSL $ will only iterate matching images)",
         "按标签预筛选（DSL 中的 $ 仅遍历匹配图片）"},
        {"Add Tag", "新增标签"},
        {"Clear All", "清除所有筛选"},
        {"Cancel", "取消"},
        {"Apply", "应用筛选"},
        {"Tag name", "标签名"},
        {"+ new value, press Enter to confirm", "+ 新值，回车确认"},
        {"Click to remove this value", "点击移除该值"},
        {"Remove this condition", "删除该标签条件"},
        {"DSL Code (editable)", "DSL 代码（可手动编辑）"},
        {"Generated DSL appears here; edit and click Search.",
         "此处自动填充 LLM 生成的 DSL，可手动修改后点击「执行检索」。"},
        {"📁 Library: %1", "📁 图库: %1"},
        {"🧠 %1", "🧠 %1"},
        {"🔌 Extensions: %1", "🔌 扩展: %1"},
        {"⚡ Cache hit: %1%", "⚡ 缓存命中率: %1%"},
        {"Ready", "就绪"},
        {"Please enter a description first", "请先输入自然语言描述"},
        {"API key not set - open Settings -> API Config", "未配置 API Key，请打开 设置 -> API 配置"},
        {"Requesting LLM...", "正在请求 LLM..."},
        {"LLM generated DSL - click Search to run", "LLM 已生成 DSL，可点击「执行检索」运行"},
        {"DSL is empty - translate or type it first", "DSL 为空，请先翻译或手动输入"},
        {"Engine not found: %1", "找不到引擎: %1"},
        {"Engine is already running", "引擎正在运行中，请稍候"},
        {"Running engine...", "正在执行引擎..."},
        {"Unable to start engine process", "无法启动引擎进程"},
        {"Engine output is not valid JSON", "引擎输出不是有效 JSON"},
        {"%1 matches (showing %2, %3 filtered as low-score)",
         "检索到 %1 张匹配图片（显示 %2 张，过滤低分 %3 张）"},
        {"Image Retrieval DSL Tool", "图像检索 DSL 工具"},
        {"General", "通用"},
        {"API Config", "API 配置"},
        {"Library", "图库"},
        {"Models", "模型"},
        {"Inference", "推理阈值"},
        {"Extensions", "扩展包"},
        {"Logs", "日志"},
        {"General Settings", "通用设置"},
        {"Inference Thresholds", "推理阈值设置"},
        {"Base Confidence:", "基础置信度:"},
        {"IoU (NMS):", "IoU (NMS):"},
        {"Fallback Threshold:", "回退阈值:"},
        {"Changes take effect on the next search. Set Fallback to 0 to disable it "
         "(the OIV7 model already predicts parent classes directly).",
         "修改将在下次检索时生效。将「回退阈值」设为 0 可禁用回退（OIV7 模型已能直接输出父类别）。"},
        {"Language:", "语言:"},
        {"Auto-load last search on startup", "启动时自动加载上次搜索"},
        {"Base URL:", "Base URL:"},
        {"API Key:", "API Key:"},
        {"Model Name:", "模型名称:"},
        {"Test Connection", "测试连接"},
        {"Add Folder", "添加文件夹"},
        {"Remove Selected", "删除所选"},
        {"Reindex", "重新索引"},
        {"Add Model", "添加模型包"},
        {"Remove Model", "删除模型包"},
        {"Add Extension", "添加扩展包"},
        {"Remove Extension", "删除扩展包"},
        {"Connection Test", "连接测试"},
        {"Connection OK", "连接成功"},
        {"Connection Failed", "连接失败"},
        {"Double-click an item to switch the active model (writes registry.json):",
         "双击列表项切换激活模型（将写入 registry.json）："},
        {"Check to enable/disable an extension pack (writes registry.json):",
         "勾选启用/禁用扩展包（实时写入 registry.json）："},
        {"You are up to date.", "当前已是最新版本。"},
        {"Cleared cache for %1; next search rebuilds.", "已清空模型 %1 的缓存，下次检索自动重新索引。"},
        {"Cache is already up to date.", "缓存已是最新或无需清理。"},
        {"LLM request failed: %1", "LLM 请求失败: %1"},
        {"Engine error: %1", "引擎错误: %1"},
        {"Result (scalar): %1", "结果(标量): %1"},
        {"Model: [%1]    Extensions: [%2]    Library: %3 images",
         "模型: [%1]    扩展包: [%2]    图库: %3 张图"},
        {"Scene", "场景"},
        {"Indoor", "室内"},
        {"Top scenes", "Top 场景"},
        {"Scene recognition unavailable", "场景识别不可用"},

        // ---- albums / smart albums ----
        {"Save as Smart Album", "保存为智能相簿"},
        {"Export Report", "导出报告"},
        {"Batch Edit", "批量编辑"},
        {"Albums", "相簿"},
        {"Smart Albums", "智能相簿"},
        {"New album", "新建相簿"},
        {"New Album", "新建相簿"},
        {"New Album...", "新建相簿..."},
        {"Album name:", "相簿名称:"},
        {"Rename...", "重命名..."},
        {"Delete", "删除"},
        {"Rename", "重命名"},
        {"New name:", "新名称:"},
        {"Rename failed (name may already be in use).", "重命名失败（名称可能已被占用）。"},
        {"Renamed to \"%1\".", "已重命名为 \"%1\"。"},
        {"Delete Album", "删除相簿"},
        {"Delete album \"%1\"? (images on disk are kept)", "确定删除相簿 \"%1\"？（磁盘文件不会被删除）"},
        {"Added %1 image(s) to \"%2\".", "已将 %1 张图片添加到 \"%2\"。"},
        {"Removed %1 image(s) from \"%2\".", "已从 \"%2\" 移除 %1 张图片。"},
        {"No image selected.", "未选中图片。"},
        {"Add to Album", "添加到相簿"},
        {"(no albums - create one)", "（暂无相簿，请先创建）"},
        {"Remove from Album", "从相簿移除"},
        {"Details", "详情"},
        {"Album \"%1\" already exists or could not be created.", "相簿 \"%1\" 已存在或创建失败。"},
        {"Created album \"%1\".", "已创建相簿 \"%1\"。"},
        {"Smart album name:", "智能相簿名称:"},
        {"DSL is empty - nothing to save.", "DSL 为空，无可保存的内容。"},
        {"Saved smart album \"%1\".", "已保存智能相簿 \"%1\"。"},
        {"%1 images", "%1 张图片"},

        // ---- export report ----
        {"Format:", "格式:"},
        {"HTML", "HTML"},
        {"PDF", "PDF"},
        {"Scope:", "范围:"},
        {"All results (%1)", "全部结果 (%1)"},
        {"Selected only (%1)", "仅选中 (%1)"},
        {"Export", "导出"},
        {"Nothing to export.", "没有可导出的内容。"},
        {"Save Report", "保存报告"},
        {"PDF files (*.pdf)", "PDF 文件 (*.pdf)"},
        {"HTML files (*.html)", "HTML 文件 (*.html)"},
        {"Report saved to:\n%1", "报告已保存到:\n%1"},
        {"Failed to write report.", "写入报告失败。"},
        {"Scenes", "场景"},
        {"Objects", "物体"},
        {"Tags", "标签"},
        {"File", "文件"},
        {"Path", "路径"},
        {"Size", "尺寸"},
        {"Modified", "修改时间"},
        {"Score", "评分"},
        {"Camera", "相机"},
        {"Shutter", "快门"},
        {"Aperture", "光圈"},
        {"Focal", "焦距"},
        {"Date", "日期"},
        {"Exposure", "曝光"},
        {"over", "过曝"},
        {"under", "欠曝"},
        {"good", "良好"},
        {"Sharpness", "清晰度"},
        {"Image Retrieval Report", "图片检索报告"},
        {"Generated", "生成时间"},
        {"Images", "图片数"},

        // ---- batch edit ----
        {"Batch Edit", "批量编辑"},
        {"Tags", "标签"},
        {"Add tag (key):", "添加标签（键）:"},
        {"Value:", "值:"},
        {"Add / Update Tag", "添加 / 更新标签"},
        {"Remove tag:", "删除标签:"},
        {"Remove Tag", "删除标签"},
        {"Rating:", "评分:"},
        {"Set Rating", "设置评分"},
        {"Rename Files", "重命名文件"},
        {"Template:", "模板:"},
        {"e.g. 2024-{date}-{scene}-{index}", "例如 2024-{date}-{scene}-{index}"},
        {"Preview", "预览"},
        {"Apply Rename", "执行重命名"},
        {"Placeholders: {index} (1,2,...)  {date} (YYYYMMDD)  {scene}  {object}",
         "占位符: {index} (1,2,...)  {date} (日期)  {scene} (场景)  {object} (物体)"},
        {"Original", "原文件名"},
        {"New", "新文件名"},
        {"Status", "状态"},
        {"Close", "关闭"},
        {"Cannot read cache index.", "无法读取缓存索引。"},
        {"Failed to write cache index.", "写入缓存索引失败。"},
        {"Applied to %1 image(s).", "已应用到 %1 张图片。"},
        {"Tag key is empty.", "标签名为空。"},
        {"Choose a tag to remove.", "请选择要删除的标签。"},
        {"Template is empty.", "模板为空。"},
        {"unchanged", "未变化"},
        {"CONFLICT - target exists", "冲突 - 目标文件已存在"},
        {"ok", "就绪"},
        {"Preview: %1 rename(s) ready. Click \"Apply Rename\" to confirm.",
         "预览: 共 %1 个重命名就绪。点击「执行重命名」确认。"},
        {"Run Preview first (no valid renames).", "请先预览（当前没有可执行的重命名）。"},
        {"Renamed %1 image(s).", "已重命名 %1 张图片。"},
        {"Failed: %1", "失败: %1"},
        {"Nothing was renamed.", "没有文件被重命名。"},
        {"All targets failed.", "所有目标文件均重命名失败。"},

        // ---- clustering groups ----
        {"Rename Group", "重命名分组"},
        {"Display name for this group:", "此分组的显示名称:"},
        {"Group \"%1\": %2 photo(s)", "分组 \"%1\": %2 张照片"},
    };
    return m;
}

}  // namespace

QString ZhTranslator::translate(const char* context, const char* sourceText,
                                const char* disambiguation, int n) const {
    if (sourceText) {
        auto it = zhMap().find(QString::fromUtf8(sourceText));
        if (it != zhMap().end()) return it.value();
    }
    return QTranslator::translate(context, sourceText, disambiguation, n);
}

LanguageManager* LanguageManager::instance() {
    static LanguageManager mgr;
    return &mgr;
}

LanguageManager::LanguageManager(QObject* parent) : QObject(parent) {
    // install translator immediately if zh is the saved language
    if (SettingsManager::instance()->value("language").toString() == "zh") {
        qApp->installTranslator(&translator_);
    }
}

QString LanguageManager::language() const {
    return SettingsManager::instance()->value("language", "en").toString();
}

void LanguageManager::setLanguage(const QString& lang) {
    SettingsManager::instance()->setValue("language", lang);
    qApp->removeTranslator(&translator_);
    if (lang == "zh") {
        qApp->installTranslator(&translator_);
    }
    emit languageChanged();
}