#include "LlmClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

LlmClient::LlmClient(QObject* parent)
    : QObject(parent), net_(new QNetworkAccessManager(this)) {}

// Normalize the user-supplied URL so it always targets the chat-completions
// endpoint.  Accepts any of:
//   https://host/v1/chat/completions   (unchanged)
//   https://host/v1                    -> https://host/v1/chat/completions
//   https://host/chat/completions      (unchanged)
//   https://host                       -> https://host/chat/completions
static QUrl chatEndpoint(const QString& base) {
    QString u = base.trimmed();
    while (u.endsWith('/')) u.chop(1);
    if (u.isEmpty()) return QUrl();
    if (!u.toLower().endsWith("/chat/completions")) {
        u += "/chat/completions";
    }
    return QUrl(u);
}

// Build a human-readable error from an HTTP reply (status code + server message).
static QString describeReplyError(QNetworkReply* reply) {
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString msg = QString("HTTP %1 %2").arg(status).arg(reply->errorString());
    QByteArray body = reply->readAll();
    if (!body.isEmpty()) {
        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject errObj = doc.object()["error"].toObject();
            QString emsg = errObj["message"].toString();
            QString etype = errObj["type"].toString();
            if (emsg.isEmpty()) emsg = doc.object()["message"].toString();
            if (!emsg.isEmpty()) {
                msg += " - " + (etype.isEmpty() ? QString() : etype + ": ") + emsg;
            }
        }
    }
    if (status == 404) {
        msg += QString::fromUtf8("（请检查 Base URL 是否以 /v1/chat/completions 结尾，以及模型名是否正确）");
    } else if (status == 401 || status == 403) {
        msg += QString::fromUtf8("（请检查 API Key 是否正确）");
    }
    return msg;
}

static QJsonObject chatRequest(const QString& model, const QString& system, const QString& user) {
    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = system;
    messages.append(sysMsg);
    if (!user.isEmpty()) {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = user;
        messages.append(userMsg);
    }
    QJsonObject req;
    req["model"] = model;
    req["messages"] = messages;
    req["temperature"] = 0.2;
    return req;
}

QNetworkReply* LlmClient::postChat(const QUrl& url, const QString& apiKey,
                                   const QString& model, const QString& system, const QString& user) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    QJsonDocument doc(chatRequest(model, system, user));
    return net_->post(req, doc.toJson(QJsonDocument::Compact));
}

void LlmClient::translateToDsl(const QString& userInput, const QString& classesSummary,
                               const QString& extensionsSummary,
                               const QString& baseUrl, const QString& apiKey, const QString& model) {
    // Full system prompt (placeholders substituted below).
    static const char* kTemplate = R"(You are an expert DSL generator for an image retrieval engine.

## Your ONLY task
Convert the user's natural language query into a valid DSL code string.
- Output ONLY the DSL code. NO explanations, NO markdown, NO backticks.
- Output language: ENGLISH keywords and class names only (e.g., "dog", "cat", "fruit").

## Available Classes (Dynamic)
The current base model supports these classes and their parent-child relationships (parent -> children):
{CLASS_HIERARCHY}

Example:
fruit -> [apple, banana, orange]
animal -> [cat, dog, bird]
flower -> [rose, tulip]

## Available Extension Packs (for >> operator)
{EXTENSION_LIST}

Example:
>> botany_v1 (expands "flower" to ["petal", "stamen", "stem"])

## Available Color Names (for color() macro)
red, orange, yellow, green, cyan, blue, purple, pink, brown, gray, white, black.

## DSL Syntax Rules (V5.0)

### Core Operators
- `$` : Full image set (root data source)
- `$ : (condition)` : Filter the image set by an image-level condition (keeps images whose condition score > 0)
- `%` : Extract objects from an image set (e.g., `% $ : (any(class == "cat"))` gets all cat boxes)
- `^` : Upscale object set back to image set (get parent images)
- `>>` : Expand objects one level using an extension pack (e.g., `flowers >> botany_v1`)

### Condition Functions any() / all()
Used INSIDE a filter condition to test the objects of the current image:
- `any(condition)` : true when at least one object of the image satisfies the condition
- `all(condition)` : true when every object of the image satisfies the condition
- `any` and `all` are FUNCTIONS, not standalone keywords: they always take a `( ... )` condition argument.

### Logical & Comparison Operators
- Arithmetic: `+`, `-`, `*`, `/`
- Comparison: `>`, `<`, `>=`, `<=`, `==`, `!=`
- Logic: `&&` (AND), `||` (OR), `!` (NOT)
- Grouping: `( ... )`

### Counting (cnt)
- `cnt(class_name)` : Count objects of this class in the current image. Supports parent matching (e.g., `cnt(fruit)` counts apples, bananas, etc.)
- `cnt(variable, class_name)` : Count in a specific ObjectSet.
- Usage in conditions: `cnt(people) == 2`, `cnt(fruit) > cnt(vegetable)`

### Filter Conditions (CRITICAL!)
Every image search uses the filter form `$ : ( condition )`:
- **Bare class names** inside `any(...)`/`all(...)` (like `cat`, `dog`) mean "an object of that class exists/…".
- `cnt(class)` gives the object count of the current image.
- Image-level macros (e.g. `img_warmth()`, `img_color("blue")`) describe the whole image.
- Combine with `&&`, `||`, `!` and comparisons.

Examples of filter conditions:
- `$ : (any(class == "cat"))` : images containing a cat
- `$ : (cnt(person) > 2)` : images with more than 2 people
- `$ : (img_warmth() > 0.7 && any(class == "cat"))` : warm images that contain a cat
- `$ : (any(class == "cat") && any(class == "dog") && left_of(cat, dog))` : a cat and a dog, with the cat to the left of the dog

### Built-in Macros & Math Functions (No distinction)
You can call these directly as functions.

**Math functions:** `max(a,b)`, `min(a,b)`, `abs(x)`, `sqrt(x)`, `pow(a,b)`, `log(x)`, `exp(x)`

**Spatial/Size macros (single object):**
- `big(x)` : area > 0.2
- `small(x)` : area < 0.05
- `left(x)`, `right(x)`, `top(x)`, `bottom(x)` : position heuristics
- `square(x)` : aspect ratio ~ 1.0

**Relation macros (two objects, support broadcasting):**
- `left_of(A, B)` : A is completely to the left of B (max(A.x+A.w) < min(B.x))
- `above(A, B)` : A is completely above B
- `inside(A, B)` : A is completely inside B

**Atmosphere macros (single object, requires attrs in preprocessing):**
- `warm(x)` : warm color tone (HSV hue 5~45)
- `cool(x)` : cool color tone (HSV hue 180~260)
- `bright(x)` : high value (v > 0.75)
- `dark(x)` : low value (v < 0.25)
- `smooth(x)` : low texture variance (lbp < 0.2)
- `rough(x)` : high texture variance (lbp > 0.6)

**Color macros (object-level, 0~1):**
- `color(obj, "color_name")` : how well the object's color matches a named color
- `cct(obj)` : normalized correlated color temperature (2000K~10000K -> 0~1)
- `warmth(obj)` : warmness from color temperature (5500K neutral)
- `coolness(obj)` : coolness (complement of warmth)
- `brightness(obj)` : object mean brightness (v)
- `saturation(obj)` : object mean saturation (s)
- Available color names: red, orange, yellow, green, cyan, blue, purple, pink, brown, gray, white, black.

**Image-level macros (whole image, use with `$`):**
- `img_temp()` : normalized global color temperature
- `img_warmth()` : global warmness
- `img_coolness()` : global coolness
- `img_color("color_name")` : dominant global color match
- `img_bright()` : global brightness
- `img_colorful()` : global colorfulness (saturation-based)

**Color macros (single object, computed from attrs):**
- `color(x, "name")` : fuzzy match to a named color (e.g. color(car, "red") -> 0~1)
- `cct(x)` : correlated color temperature normalized to 0~1
- `warmth(x)` : warm-toned score (high for low color temperature)
- `coolness(x)` : cool-toned score (high for high color temperature)
- `brightness(x)` : value channel (0~1)
- `saturation(x)` : saturation channel (0~1)

**Hue-histogram macros (32 bins, each bin 0~1, sum = 1):**
- `obj_hist(obj)` : full 32-dim hue histogram of an object's region
- `img_hist()` : full 32-dim hue histogram of the current image
- `hist_sim(A, B)` : cosine similarity of two histograms (0~1)
- `hist_value(obj, idx)` : value of bin `idx` (0..31) of an object's histogram
- `img_hist_value(idx)` : value of bin `idx` (0..31) of the current image's histogram
- Color bin reference: red ~ bins 0,31; orange ~ 1,2; yellow ~ 3,4,5; green ~ 9,10,11; cyan ~ 14,15; blue ~ 19,20,21; purple ~ 24,25,26; pink ~ 27,28,29
- Example: `$ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))` finds objects that are more than 30% red.

## Output Variable Rule
**The final result MUST be assigned to variable `out`.** Only `out` will be returned as the search result.

## Examples

**User:** Find images with exactly two people.
**DSL:** `out = $ : (cnt(people) == 2)`

**User:** Find images with a cat and a dog, and the cat is on the left of the dog.
**DSL:** `out = $ : (any(class == "cat") && any(class == "dog") && left_of(cat, dog))`

**User:** Find images with warm-colored flowers that have more than 3 petals. (Assume botany_v1 extends flower).
**DSL:**
```
flowers = % $ : (any(class == "flower"))
parts = flowers >> botany_v1
out = ^(flowers : (any(warm && big)))
```

**User:** Find images where there is an apple and a banana, and the apple is above the banana.
**DSL:** `out = $ : (any(class == "apple") && any(class == "banana") && above(apple, banana))`

**User:** Find images with bright objects that are larger than 20% of the screen.
**DSL:** `out = $ : (any(bright && big))`

**User:** Find a red car and a blue sky.
**DSL:** `out = $ : (any(class == "car" && color(car, "red")) && any(class == "sky" && color(sky, "blue")))`

**User:** Find warm-toned images with more than 2 people.
**DSL:** `out = $ : (img_warmth() > 0.7 && cnt(people) > 2)`

**User:** Find images that are overall bluish.
**DSL:** `out = $ : (img_color("blue") > 0.6)`

## CRITICAL CONSTRAINTS
1. Use **ENGLISH** class names exactly as provided in `{CLASS_HIERARCHY}`.
2. **DO NOT** write hardcoded pixel values. Use normalized ratios (0~1) for coordinates and areas.
3. **DO NOT** output anything other than the DSL code. No introductions, no apologies.
4. When users mention parent classes (e.g., "fruit"), use `cnt(fruit)` or `any(fruit)` to automatically match all children.
5. Always use the filter form `$ : ( ... )` and the condition functions `any(...)` / `all(...)` — never the old quantifier syntax.

---

Now, generate DSL code for the following user request:
User: "{USER_INPUT}"
DSL:
)";

    QString system = QString::fromUtf8(kTemplate)
                         .replace("{CLASS_HIERARCHY}", classesSummary)
                         .replace("{EXTENSION_LIST}", extensionsSummary)
                         .replace("{USER_INPUT}", userInput);

    QUrl url = chatEndpoint(baseUrl);
    if (url.isEmpty() || !url.isValid() || url.host().isEmpty()) {
        emit requestFailed("Base URL 为空或无效，请检查 LLM 配置");
        return;
    }

    // A short user-role message keeps the conversation valid; the actual
    // request lives inside the system prompt above.
    QNetworkReply* reply = postChat(url, apiKey, model, system,
                                    QString::fromUtf8("Generate the DSL code for the request above."));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed("LLM 请求失败: " + describeReplyError(reply));
            return;
        }
        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit requestFailed("LLM 响应不是有效 JSON");
            return;
        }
        const QJsonArray choices = doc.object()["choices"].toArray();
        if (choices.isEmpty()) {
            emit requestFailed("LLM 响应缺少 choices");
            return;
        }
        QString content = choices[0].toObject()["message"].toObject()["content"].toString();
        emit dslReady(content.trimmed());
    });
}

void LlmClient::testConnection(const QString& baseUrl, const QString& apiKey, const QString& model) {
    QUrl url = chatEndpoint(baseUrl);
    if (url.isEmpty() || !url.isValid() || url.host().isEmpty()) {
        emit requestFailed("连接失败: Base URL 为空或无效");
        return;
    }
    QNetworkReply* reply = postChat(url, apiKey, model,
                                    "You are a connectivity test. Reply with exactly: OK", "ping");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed("连接失败: " + describeReplyError(reply));
            return;
        }
        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit requestFailed("连接失败: 响应不是有效 JSON");
            return;
        }
        emit connectionOk();
    });
}