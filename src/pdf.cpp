// STL
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <numeric>
#include <variant>
#include <locale>
#include <codecvt>
#include <cassert>
#include <filesystem>
// fmt format
#include <fmt/format.h>
#include <fmt/xchar.h>
// local lxd
#include "../lxd/src/fileio.h"
#include "../lxd/src/encoding.h"
#include "../lxd/src/str.h"
// windows api
#include <Windows.h>
#include <cstring>
#include <atlstr.h>
#include <wingdi.h>
// stb 
#define STB_IMAGE_IMPLEMENTATION
#include "../util/stb_image.h"
// timer
#include "../util/cxxtimer.hpp"

#ifdef DrawText
#undef DrawText
#endif

#ifdef LoadImage
#undef LoadImage
#endif

#define DELETE_TXT_FILE

namespace pdf {
	using namespace std;
	// Concepts Constraints
	template<typename T>
	concept hasContent = requires(T t) { t.Content(); };

	template <typename T>
	concept component = std::is_base_of<Component<T>, T>::value;

	template <class T>
	void print(std::initializer_list<T> elems) {
		for (const auto& elem : elems) {
			std::cout << elem << " ";
		}
		std::cout << std::endl;
	}

#define FLOAT_EQUAL(f0, f1) (abs((f1) - (f0)) < 0.0001)

	enum class LANGUAGE {
		ENGLISH,
		CHINESE,
		ESCAPE_CHAR,
		SPACING
	};

	struct Vector2 {
		float x = {};
		float y = {};
	};

	struct Vector3 {
		float x = {};
		float y = {};
		float z = {};
	};

	typedef Vector2 Position;
	typedef Vector3 Color3;

	enum class ALIGNMENT {
		DEFAULT,
		LEFT,
		CENTER,
		RIGHT,
	};

	enum class FIGURE {
		DEFAULT,
		TEXT,
		RECT,
		LINE,
		IMAGE
	};

	enum class TOOTH_IMAGE {
		NORMAL,
		ATTACHMENT,
		MISSING
	};

	// PDF Canvas Size
	const size_t PDF_WIDTH = 707;
	const size_t PDF_HEIGHT = 1000;
	const size_t PDF_BOTTOM = 900;

	// Padding 
	const size_t PDF_PADDING = 50;
	const size_t PDF_LINE_PADDING = 8;
	const size_t PDF_SECTION_PADDING = 20;
	
	struct TextStyle {
		bool bold = false;
		bool itallic = false;
		bool underline = false;
		ALIGNMENT alignment = ALIGNMENT::DEFAULT;
		Vector2 alignmentRange = { 0.0 + PDF_PADDING,  PDF_WIDTH - PDF_PADDING };
	};

	struct AttachmentInfo {
		AttachmentInfo(int32_t fdi, const std::string& attName, int32_t startStep, int32_t endStep)
			: fdi(fdi), attName(attName), startStep(startStep), endStep(endStep) {}

		int32_t fdi = {};
		int32_t startStep = {}, endStep = {};
		std::string attName;
	};

	struct ImageInfo {
		std::string m_imageId;
		Vector2 horzRange = {};
		Vector2 drawPosition = {};
		int32_t width = {}, height = {};
		float scaling = 1.0;
	};

	std::string UnicodeToUtf8(const std::wstring_view wstr) {
#ifdef USE_LXD
		return lxd::utf8_encode(wstr);
#else 
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.to_bytes(std::wstring(wstr));
#endif
	}

	std::wstring Utf8ToUnicode(const std::string_view str) {
#ifdef USE_LXD
		return lxd::utf8_decode(str);
#else 
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.from_bytes(std::string(str));
#endif
	}

	static std::map<char, float> CharWidthPool;

	void InitCharWidthPool() {
		for (char ch = (char)33; ch < (char)126; ch++) {
			auto hdc = GetDC(NULL);
			LPCSTR lpcCh(&ch);
			SIZE sz;
			GetTextExtentPoint32(hdc, lpcCh, 1, &sz);
			auto magicNumber = 1.05;
			CharWidthPool[ch] = sz.cx;
		}
	}

	LANGUAGE GetLanguage(uint32_t unicode) {
		auto inRange = [](uint32_t uc, uint32_t rBeg, uint32_t rEnd) {
			if (rBeg > rEnd) std::swap(rBeg, rEnd);
			return uc >= rBeg && uc <= rEnd;
		};

		// Basic Chinese Unicode Range
		if (inRange(unicode, 0x4E00, 0x9FA5) ||
			inRange(unicode, 0xFF01, 0xFF1B) ||
			inRange(unicode, 0x3001, 0x300F) ||
			inRange(unicode, 0x2018, 0x201D)) {
			return LANGUAGE::CHINESE;
		}
		else if ((unicode == '\n') || (unicode == '\r') || (unicode == '\t')) {
			return LANGUAGE::ESCAPE_CHAR;
		}
		else if (unicode == ' ') {
			return LANGUAGE::SPACING;
		}
		else {
			return LANGUAGE::ENGLISH;
		}
	}

	template<class T>
	class Component {
	public:
		Component() {
			++m_componentCount;
		}

		Component(Vector2 startPosition, Vector2 size = { 0.0, 0.0 })
			: m_startPosition(startPosition), m_size(size)
		{
			++m_componentCount;
		}

		Vector2 Size() const { return static_cast<T*>(this)->Size(); }

		int32_t Count() const { return static_cast<T*>(this)->Count(); }

		std::vector<std::string> GetContent() const { return static_cast<T*>(this)->GetContent(); }

		Vector2 StartPosition() const { return static_cast<T*>(this)->StartPosition(); }

	public:
		template<component U>
		T& Append(U component) { static_cast<U*>(this)->Append(component); return static_cast<T&>(*this); }

	protected:
		inline static int32_t m_componentCount = {};

		Vector2 m_size;
		Vector2 m_startPosition;

		std::string m_content;
	};

	class Character {
	public:
		// regular i
		Character(const wchar_t character, LANGUAGE lang, float fontSize, float charItvlRatio, bool bold)
			: lang(lang), fontSize(fontSize), charItvlRatio(charItvlRatio), bold(bold)
		{
			auto interval = (charItvlRatio > 1.0) ? (charItvlRatio - 1.0) * fontSize : 0.0;

			// font size calculation
			if (lang == LANGUAGE::CHINESE) {
				this->charLen = fontSize;
				this->length = fontSize + interval;

				this->content = fmt::format("{:x}", (size_t)character);
			}
			else if (lang == LANGUAGE::ENGLISH) {
				auto magicNumber = 1.05;
				this->charLen = CharWidthPool[(char)character] * (fontSize / 16.0) * magicNumber;
				this->length = this->charLen + interval;

				this->content = (char)character;
			}
			else if (lang == LANGUAGE::ESCAPE_CHAR) {
				if (character == L'\t') {
					this->fontSize = 0.0;
					this->charLen = this->length = 2.0 * fontSize;
				}
				else {
					this->charLen = this->length = this->fontSize = 0.0;
				}
				this->content = (char)character;
			}
			else if (lang == LANGUAGE::SPACING) {
				this->length = this->charLen = 0.3 * fontSize;
			}
		}

		// Spacing constructor
		Character(LANGUAGE lang, float spacing) : lang(lang) {
			this->length = this->charLen = spacing;
		}

	public:
		// character language
		LANGUAGE lang;
		// bare character length
		float charLen;
		// total length of a single character: including the character intervals
		float length;
		// character fontSize(not the same as charLen)
		float fontSize;
		// the interval between characters
		float charItvlRatio;
		// characters absolute position on pdf canvas
		Vector2 position;
		// the character style
		bool bold = false;
		// the final content output to txt file 
		std::variant<char, std::string> content;
	};

	class Text : Component<Text> {
	public:
		Text() : m_fontSize(12.0), m_range(Vector2{ PDF_PADDING, PDF_WIDTH - PDF_PADDING }), m_charItvlRatio(1.0), m_lineItvl(1.2) {}

		Text(const std::wstring_view text, float fontSize, float charNumIndent = 0.0, bool bold = false)
			: m_vanillaText(text), m_fontSize(fontSize),
			m_range(Vector2{ PDF_PADDING, PDF_WIDTH - PDF_PADDING }),
			m_charItvlRatio(1.0), m_lineItvl(1.2)
		{
			float indent = charNumIndent * m_fontSize;
			m_text.emplace_back(LANGUAGE::SPACING, indent);

			for (const auto& ch : m_vanillaText) {
				auto lang = GetLanguage(ch);
				m_text.emplace_back(ch, lang, m_fontSize, m_charItvlRatio, bold);
			}
		}

		Text(const std::wstring_view text, float fontSize, Vector2 pos, bool bold = false)
			: Component(pos), m_vanillaText(text), m_fontSize(fontSize),
			m_range(Vector2{ PDF_PADDING, PDF_WIDTH - PDF_PADDING }), m_charItvlRatio(1.0), m_lineItvl(1.2)
		{
			m_startPosition.y = PDF_HEIGHT - pos.y;

			for (const auto& ch : m_vanillaText) {
				auto lang = GetLanguage(ch);
				m_text.emplace_back(ch, lang, m_fontSize, m_charItvlRatio, bold);
			}
		}

		Text(const std::wstring_view text, float fontSize, float depth, ALIGNMENT alignment, bool bold = false)
			: Component({ 0, depth }), m_vanillaText(text), m_fontSize(fontSize),
			m_range(Vector2{ PDF_PADDING, PDF_WIDTH - PDF_PADDING }), m_charItvlRatio(1.0), m_lineItvl(1.2), m_alignment(alignment)
		{
			m_startPosition.y = PDF_HEIGHT - depth;

			for (const auto& ch : m_vanillaText) {
				auto lang = GetLanguage(ch);
				m_text.emplace_back(ch, lang, m_fontSize, m_charItvlRatio, bold);
			}
		}

	public:
		void CalcLayout() {
			static bool run_once = true;

			auto& t = m_text;
			auto textHandling = [&]() {
				Vector2 curPos = { m_range.x, m_startPosition.y };
				int i = 0;
				for (auto& ch : m_text) {
					bool lineOverflow = static_cast<int>(curPos.x + ch.charLen) > m_range.y;
					bool EOL = (ch.lang == LANGUAGE::ESCAPE_CHAR) && (std::get<char>(ch.content) == '\n');

					if (lineOverflow || EOL) {
						// std::cout << "line overflow: " << curPos.x << '\t' << i << std::endl;
						m_lineCount++;
						curPos.y -= m_fontSize * m_lineItvl;
						curPos.x = m_range.x;

						if (m_autoNextPage && (curPos.y < PDF_PADDING)) {
							curPos.x = m_range.x;
							curPos.y = PDF_HEIGHT - PDF_PADDING;
						}
					}

					ch.position = curPos;
					curPos.x += ch.length;
					i++;
					// if (run_once) std::cout << "curPos.x: " << curPos.x << std::endl;
				}
			};

			m_lineCount = 0;
			switch (m_alignment) {
			case ALIGNMENT::DEFAULT: {
				m_range = Vector2{ m_startPosition.x, PDF_WIDTH - PDF_PADDING };
				textHandling();
				break;
			}
			case ALIGNMENT::LEFT: {
				m_range = Vector2{ PDF_PADDING, PDF_WIDTH - PDF_PADDING };
				textHandling();
				break;
			}
			case ALIGNMENT::CENTER: {
				auto textLength = GetLength();
				auto itvlCompensation = (m_text.back().length - m_text.back().charLen) / 2.0;
				float offset = (m_range.x + m_range.y) / 2.0 - textLength / 2.0 + itvlCompensation;
				m_range = Vector2{ offset, PDF_WIDTH - PDF_PADDING };
				textHandling();
				break;
			}
			case ALIGNMENT::RIGHT: {
				auto textLength = GetLength();
				auto itvlCompensation = m_text.back().length - m_text.back().charLen;
				auto offset = m_range.y - textLength + itvlCompensation;
				m_range.x = offset;
				textHandling();
				break;
			}
			}
			run_once = false;
		}

		std::vector<std::string> GetContent() const {
			if (!m_text.size()) return std::vector<std::string>{};

			std::vector<std::string> textVec;
			textVec.emplace_back();
			auto ret = &textVec[0];

			// state control vars
			auto preLang = m_text[0].lang;
			auto prePos = m_text[0].position;
			auto preBold = m_text[0].bold;
			auto xInc = m_range.x;
			std::string buffer;

			// lambda handle func
			auto hanndleSpecialChar = [&buffer]() {
				auto pos = buffer.find('(');

				if (pos != std::string::npos) {
					buffer.insert(pos, "\\");
				}

				pos = buffer.find(')');

				if (pos != std::string::npos) {
					buffer.insert(pos, "\\");
				}
			};

			// 写入文件的lambda func
			auto writeToFile = [&](int32_t i) {
				// 将mutool命令冲突字符转义
				hanndleSpecialChar();

				// 处理spacing char对象
				if (preLang == LANGUAGE::SPACING) {
					preLang = m_text[i].lang;
					prePos = m_text[i].position;
					return;
				}

				auto writable = (preLang == LANGUAGE::CHINESE) || (preLang == LANGUAGE::ENGLISH);

				auto font = (preLang == LANGUAGE::CHINESE) ? "Song" : "TmRm";

				std::string content;
				if (preLang == LANGUAGE::CHINESE)
					content = fmt::format("<{}>", buffer);
				else if (preLang == LANGUAGE::ENGLISH)
					content = fmt::format("({})", buffer);

				auto bufferFontSize = m_text[i - 1].fontSize;
				auto charItvl = (m_charItvlRatio - 1.0) * bufferFontSize;

				// draw content
				if (writable) {
					if (preBold) {
						// ret->append(fmt::format("q\nBT /{} {} Tf 2 Tr 1 0 0 1 {} {} Tm {} {} {} \" ET\nQ\r\n",
						// 	font, bufferFontSize, prePos.x, prePos.y, 0, charItvl, content));
						auto offset = 0.2 * (bufferFontSize / 16.0);
						ret->append(fmt::format("BT /{} {} Tf 1 0 0 1 {} {} Tm {} {} {} \" ET\r\n",
							font, bufferFontSize, prePos.x, prePos.y, 0, charItvl, content));

						ret->append(fmt::format("BT /{} {} Tf 1 0 0 1 {} {} Tm {} {} {} \" ET\r\n",
							font, bufferFontSize, prePos.x + offset, prePos.y, 0, charItvl, content));
						ret->append(fmt::format("BT /{} {} Tf 1 0 0 1 {} {} Tm {} {} {} \" ET\r\n",
							font, bufferFontSize, prePos.x - offset, prePos.y, 0, charItvl, content));
						ret->append(fmt::format("BT /{} {} Tf 1 0 0 1 {} {} Tm {} {} {} \" ET\r\n",
							font, bufferFontSize, prePos.x, prePos.y + offset, 0, charItvl, content));
						ret->append(fmt::format("BT /{} {} Tf 1 0 0 1 {} {} Tm {} {} {} \" ET\r\n",
							font, bufferFontSize, prePos.x, prePos.y - offset, 0, charItvl, content));
					}
					else {
						ret->append(fmt::format("BT /{} {} Tf 1 0 0 1 {} {} Tm {} {} {} \" ET\r\n",
							font, bufferFontSize, prePos.x, prePos.y, 0, charItvl, content));
					}
				}

				// state update
				preBold = m_text[i].bold;
				preLang = m_text[i].lang;
				prePos = m_text[i].position;

				buffer.clear();
			};

			// main process
			for (int32_t i = 0; i < m_text.size(); i++) {
				bool EOL = (m_text[i].lang == LANGUAGE::ESCAPE_CHAR) && (std::get<char>(m_text[i].content) == '\n');
				bool overflow = static_cast<int>(xInc + m_text[i].charLen) > m_range.y;

				auto BufferAppend = [&]() {
					if (m_text[i].lang == LANGUAGE::CHINESE)
						buffer.append(std::get<std::string>(m_text[i].content));
					else if (m_text[i].lang == LANGUAGE::ENGLISH)
						buffer += std::get<char>(m_text[i].content);
				};

				if (overflow || EOL) {
					writeToFile(i);

					if (m_autoNextPage && (m_text[i].position.y > m_text[i - 1].position.y)) {
						textVec.emplace_back();
						ret = &textVec.back();
					}

					BufferAppend();

					xInc = m_range.x;
					xInc += m_text[i].length;

					if (i == m_text.size() - 1)
						writeToFile(i);

					continue;
				}

				// text style differ
				if (preBold != m_text[i].bold) {
					writeToFile(i);
				}

				// languages differ
				if (preLang != m_text[i].lang) {
					writeToFile(i);
				}

				// character appending
				BufferAppend();

				// 输出尾段文字
				if ((i == m_text.size() - 1) && !buffer.empty()) {
					writeToFile(i);
				}

				xInc += m_text[i].length;
			}

			return textVec;
		}

	public:
		float GetLength() {
			float length{};
			for (const auto& ch : m_text)
				length += ch.length;
			return length;
		}

		float GetFontSize() const {
			return m_fontSize;
		}

		Vector2 GetLastCharPosition() const {
			return m_text.back().position;
		}

		float GetBottom() const {
			if (!m_text.empty()) {
				return m_text.back().position.y;
			}
			else {
				return PDF_HEIGHT - PDF_PADDING;
			}
		}

		bool Empty() const {
			return (m_text.size() == 0);
		}

	public:
		// Text manipulation
		Text& Append(const std::wstring_view text, bool bold = false) {
			for (const auto& ch : text) {
				auto lang = GetLanguage(ch);
				m_text.emplace_back(ch, lang, m_fontSize, m_charItvlRatio, bold);
			}
			return *this;
		}

		template<component U>
		Text& Append(U component) {
			m_content.append(component.GetContent().font());
			return *this;
		}

		Text& SetFontSize(float fontSize) {
			m_fontSize = fontSize;
			for (auto& ch : m_text) {
				ch.fontSize = fontSize;
			}
			return *this;
		}

		Text& Space(float spacing = 0.0) {
			m_text.emplace_back(LANGUAGE::SPACING, spacing);
			return *this;
		}

		Text& SetIndent(float charNum) {
			auto indentLen = charNum * m_fontSize;
			m_text.emplace_back(LANGUAGE::SPACING, indentLen);
			return *this;
		}

		Text& NextLine() {
			m_text.emplace_back(L'\n', LANGUAGE::ESCAPE_CHAR, 0.0, 0.0, false);
			return *this;
		}

		Text& SetAutoNextPage(bool flag) {
			m_autoNextPage = flag;
			return *this;
		}

		Text& SetDepth(float depth) {
			m_startPosition.y = PDF_HEIGHT - depth;
			return *this;
		}

		Text& SetPosition(Vector2 position) {
			m_startPosition = position;
			auto depth = FLOAT_EQUAL(m_startPosition.y, 0.0) ? PDF_PADDING : m_startPosition.y;
			m_startPosition.y = PDF_HEIGHT - depth;
			return *this;
		}

		Text& SetAlignment(ALIGNMENT alignment, Vector2 range = { 0.0 + PDF_PADDING,  PDF_WIDTH - PDF_PADDING }) {
			this->m_alignment = alignment;
			this->m_range = range;
			return *this;
		}

		Text& SetAlignment(float depth, ALIGNMENT alignment, Vector2 range = { 0.0 + PDF_PADDING,  PDF_WIDTH - PDF_PADDING }) {
			this->m_alignment = alignment;
			this->m_range = range;
			this->m_startPosition.y = PDF_HEIGHT - depth;
			return *this;
		}

		Text& SetCharInterval(float charItvl) {
			m_charItvlRatio = charItvl;
			for (auto& ch : m_text) {
				ch.charItvlRatio = charItvl;
				auto interval = (charItvl > 1.0) ? (charItvl - 1.0) * ch.fontSize : 0.0;
				ch.length = ch.charLen + interval;
			}
			return *this;
		}

		Text& SetLineInterval(float lineItvl) {
			m_lineItvl = lineItvl;
			return *this;
		}

	public:
		Vector2 Size() { return m_size; }
		Vector2 Size() const { return m_size; }

		int32_t Count() { return m_componentCount; }
		int32_t Count() const { return m_componentCount; }

		std::string_view Content() { return m_content; }
		std::string_view Content() const { return m_content; }

		Vector2 StartPosition() { return m_startPosition; }
		Vector2 StartPosition() const { return m_startPosition; }

	private:
		// text content management member
		std::wstring m_vanillaText;
		std::vector<Character> m_text;
	private:
		// text space layout management member
		float m_fontSize;
		size_t m_lineCount = {};
		Vector2 m_range = {};
		ALIGNMENT m_alignment = ALIGNMENT::DEFAULT;
	private:
		float m_charItvlRatio;
		float m_lineItvl;
	private:
		// text style management member
		TextStyle m_textStyle;
	private:
		bool m_autoNextPage = false;
	};

	class Streak : Component<Streak> {
	public:
		Streak(Vector2 startPosition, Vector2 endPosition, Vector3 color = Vector3{ 0.0, 0.0, 0.0 })
			: Component(startPosition), m_endPosition(endPosition)
		{
			m_startPosition.y = PDF_HEIGHT - m_startPosition.y;
			m_endPosition.y = PDF_HEIGHT - m_endPosition.y;
			m_content = fmt::format("% Draw a line\r\n q {} {} {} RG {} {} m {} {} l 1.0 w S Q\r\n",
				color.x, color.y, color.z, m_startPosition.x, m_startPosition.y, m_endPosition.x, m_endPosition.y);
			++m_componentCount;
		}

	public:
		Vector2 Size() { return m_size; }
		Vector2 Size() const { return m_size; }

		int32_t Count() { return m_componentCount; }
		int32_t Count() const { return m_componentCount; }

		std::string_view Content() { return m_content; }
		std::string_view Content() const { return m_content; }

		Vector2 StartPosition() { return m_startPosition; }
		Vector2 StartPosition() const { return m_startPosition; }

	private:
		Vector2 m_endPosition;
	};

	class Rect : Component<Rect> {
	public:
		enum class Type {
			Block,
			Outline,
			BackGround
		};

	public:
		Rect(Vector2 startPosition, Vector2 size, Vector3 color, Rect::Type type)
			: Component(startPosition, size), m_type(type)
		{
			m_startPosition.y = PDF_HEIGHT - m_startPosition.y;

			switch (type) {
			case Type::Block: {
				m_content = fmt::format("% Draw a rect\r\nq {} {} {} rg {} {} {} {} re f h B Q\r\n",
					color.x, color.y, color.z, m_startPosition.x, m_startPosition.y - m_size.y, m_size.x, m_size.y);
				break;
			}
			case Type::Outline: {
				m_content = fmt::format("% Draw a Outline rect\r\nq 0.8 w {} {} {} RG {} {} {} {} re h s Q\r\n",
					color.x, color.y, color.z, m_startPosition.x, m_startPosition.y - m_size.y, m_size.x, m_size.y);
				break;
			}
			case Type::BackGround: {
				m_content = fmt::format("% Draw a rect\r\nq {} {} {} rg {} {} {} {} re f h B Q\r\n",
					color.x, color.y, color.z, m_startPosition.x, m_startPosition.y - m_size.y, m_size.x, m_size.y);
				break;
			}
			default: {
				break;
			}
			}

			++m_componentCount;
		}

	public:
		Vector2 Size() { return m_size; }
		Vector2 Size() const { return m_size; }

		int32_t Count() { return m_componentCount; }
		int32_t Count() const { return m_componentCount; }

		std::string_view Content() { return m_content; }
		std::string_view Content() const { return m_content; }

		Vector2 StartPosition() { return m_startPosition; }
		Vector2 StartPosition() const { return m_startPosition; }
	public:
		Type m_type;
	};

	class Circle : Component<Circle> {
	public:
		Circle(Vector2 startPosition, float radius, Vector3 color)
			: m_radius(radius), m_color(color)
		{
			m_startPosition = startPosition;
			m_startPosition.y = PDF_HEIGHT - m_startPosition.y;

			auto& pos = m_startPosition;
			auto ofs = m_radius * 0.553;
			m_content = fmt::format(
				"% Draw a circle\nq 0.01 w\n{} {} {} rg\n{} {} m\n{} {} {} {} {} {} c \n{} {} l\n{} {} {} {} {} {} c \n{} {} l\n{} {} {} {} {} {} c \n{} {} l\n{} {} {} {} {} {} c \nh f\nQ\n",
				color.x, color.y, color.z,
				pos.x, pos.y,
				pos.x, pos.y + ofs, pos.x + m_radius - ofs, pos.y + m_radius, pos.x + m_radius, pos.y + m_radius,
				pos.x + m_radius, pos.y + m_radius,
				pos.x + m_radius + ofs, pos.y + m_radius, pos.x + 2 * m_radius, pos.y + ofs, pos.x + 2 * m_radius, pos.y,
				pos.x + 2 * m_radius, pos.y,
				pos.x + 2 * m_radius, pos.y - ofs, pos.x + m_radius + ofs, pos.y - m_radius, pos.x + m_radius, pos.y - m_radius,
				pos.x + m_radius, pos.y - m_radius,
				pos.x + m_radius - ofs, pos.y - m_radius, pos.x, pos.y - ofs, pos.x, pos.y);
		}

	public:
		Vector2 Size() { return m_size; }
		Vector2 Size() const { return m_size; }

		int32_t Count() { return m_componentCount; }
		int32_t Count() const { return m_componentCount; }

		std::string_view Content() { return m_content; }
		std::string_view Content() const { return m_content; }

		Vector2 StartPosition() { return m_startPosition; }
		Vector2 StartPosition() const { return m_startPosition; }
	public:
		float m_radius = {};
		Vector3 m_color = {};
	};

	// TODO: A total overhaul for lazy evaluation
	class Image : Component<Image> {
	public:
		enum class Direction { Upwards, Downwards };

	public:
		Image(std::string_view m_imageId, Vector2 startPosition, Vector2 size, Direction drawDirection = Direction::Upwards)
			: Component(startPosition, size), m_imageId(m_imageId), scaling(1.0), direction(drawDirection)
		{
			m_startPosition.y = PDF_HEIGHT - m_startPosition.y;
			auto verticalStartPos = (direction == Direction::Upwards) ? m_startPosition.y : m_startPosition.y - m_size.y;
			realDrawCoord = Vector2{ m_startPosition.x, verticalStartPos };
			m_content = fmt::format("% Draw an image\r\nq {} 0 0 {} {} {} cm {} Do Q\r\n", m_size.x, m_size.y, realDrawCoord.x, realDrawCoord.y, m_imageId);
			++m_componentCount;
			++m_index;
		}

		Image(std::string_view path, Vector2 startPosition, float imageHeight, Direction drawDirection = Direction::Upwards)
			: Component(startPosition), scaling(1.0), direction(drawDirection)
		{
			m_startPosition.y = PDF_HEIGHT - m_startPosition.y;

			// config size
			int32_t w = {}, h = {}, comp = {};
			stbi_info(path.data(), &w, &h, &comp);
			scaling = imageHeight / h;
			auto width = w * scaling, height = imageHeight;
			m_size = Vector2{ width , height };

			auto verticalStartPos = (direction == Direction::Upwards) ? m_startPosition.y : m_startPosition.y - m_size.y;
			realDrawCoord = Vector2{ m_startPosition.x, verticalStartPos };

			if (std::filesystem::exists(path.data())) {
				auto imageData = fmt::format("%%Image I{} {}\r\n", m_index, path);
				m_content.append(imageData);
			}

			m_imageId = fmt::format("/I{}", m_index);

			m_content += fmt::format("% Draw an image\r\nq {} 0 0 {} {} {} cm {} Do Q\r\n", m_size.x, m_size.y, realDrawCoord.x, realDrawCoord.y, m_imageId);
			++m_componentCount;
			++m_index;
		}

	public:
		Image& SetAlignment(ALIGNMENT alignment) {
			switch (alignment) {
			case ALIGNMENT::DEFAULT: {
				m_content = fmt::format("% Draw an image\r\nq {} 0 0 {} {} {} cm {} Do Q\r\n",
					m_size.x, m_size.y, realDrawCoord.x, realDrawCoord.y, m_imageId);
				break;
			}
			case ALIGNMENT::LEFT: {
				realDrawCoord.x = PDF_PADDING;
				m_content = fmt::format("% Draw an image\r\nq {} 0 0 {} {} {} cm {} Do Q\r\n",
					m_size.x, m_size.y, realDrawCoord.x, realDrawCoord.y, m_imageId);
				break;
			}
			case ALIGNMENT::CENTER: {
				realDrawCoord.x = PDF_WIDTH / 2.0 - m_size.x / 2.0;
				m_content = fmt::format("% Draw an image\r\nq {} 0 0 {} {} {} cm {} Do Q\r\n",
					m_size.x, m_size.y, realDrawCoord.x, realDrawCoord.y, m_imageId);
				break;
			}
			case ALIGNMENT::RIGHT: {
				realDrawCoord.x = PDF_WIDTH - PDF_PADDING - m_size.x;
				m_content = fmt::format("% Draw an image\r\nq {} 0 0 {} {} {} cm {} Do Q\r\n",
					m_size.x, m_size.y, realDrawCoord.x, realDrawCoord.y, m_imageId);
				break;
			}
			}

			return *this;
		}

		Image& AttachCaption(std::wstring_view caption, float fontSize = 12.0, ALIGNMENT alignment = ALIGNMENT::CENTER) {
			float depth;

			if (direction == Direction::Downwards) {
				depth = realDrawCoord.y - fontSize * (1.0 + captionSpacing);
				depth = PDF_HEIGHT - depth;
			}
			else {
				depth = realDrawCoord.y + m_size.y + fontSize * captionSpacing;
				depth = PDF_HEIGHT - depth;
			}

			m_content.append("% Draw image caption\r\n");
			m_caption.SetFontSize(fontSize).Append(caption)
				.SetAlignment(depth, alignment, { realDrawCoord.x, realDrawCoord.x + m_size.x })
				.CalcLayout();
			m_content.append(m_caption.GetContent().front());

			return *this;
		}

		Image& SetCaptionSpacing(float spacing) {
			captionSpacing = spacing;
			return *this;
		}

		template<component U>
		Image& Append(U component) {
			m_content.append(component.GetContent().front());
			return *this;
		}

	public:
		float GetDrawPadding() const {
			auto captionPadding = m_caption.Empty() ? 0.0 : m_caption.GetFontSize();
			return PDF_SECTION_PADDING + captionPadding;
		}

	public:
		std::vector<std::string> GetContent() const {
			auto ret = std::vector<std::string>();
			ret.push_back(m_content);
			return ret;
		}

	public:
		Vector2 Size() { return m_size; }
		Vector2 Size() const { return m_size; }

		int32_t Count() { return m_componentCount; }
		int32_t Count() const { return m_componentCount; }

		std::string_view Content() { return m_content; }
		std::string_view Content() const { return m_content; }

		Vector2 StartPosition() { return m_startPosition; }
		Vector2 StartPosition() const { return m_startPosition; }

		Vector2 RealDrawPosition() { return realDrawCoord; }
		Vector2 RealDrawPosition() const { return realDrawCoord; }

	private:
		Direction direction;
		// the real draw position
		Vector2 realDrawCoord;
		// the id that's corresponding to the image path
		std::string m_imageId;
		// the image scaling 
		float scaling;
		// the text caption that's attached to the 
		Text m_caption;
		// the spacing between the caption and the image
		float captionSpacing = 0.5;
		// image index
		inline static size_t m_index = {};
	};

	class PDFTextTable {
	public:
		PDFTextTable(std::string_view tableName) : m_tableName(tableName) {
			if (!m_ctxInitialized) {
				InitContext();
				m_ctxInitialized = true;
			}
			CreatePdfFile();
		}

		~PDFTextTable() {
			for (auto& file : m_files)
				delete file;

#ifdef DELETE_TXT_FILE
			// delete the txt files
			for (int i = 0; i < m_files.size(); i++) {
				std::string txtFileName{ m_tableName };
				txtFileName.insert(txtFileName.find('.'), std::to_string(i));
				try {
					if (std::filesystem::remove(txtFileName))
						std::cout << "file " << txtFileName << " deleted.\n";
					else
						std::cout << "file " << txtFileName << " not found.\n";
				}
				catch (const std::filesystem::filesystem_error& err) {
					std::cout << "filesystem error: " << err.what() << '\n';
				}
			}
#endif
		}

	public:
		void InitContext() {
			InitCharWidthPool();
		}

		void GeneratePDF(const std::string& filePath) {
			auto pdfFilePath = fmt::format("{}{}", filePath, (filePath.find(".pdf") == std::string::npos) ? ".pdf" : "");

			std::string pageNames;
			for (size_t i = 0; i < m_files.size(); i++) {
				auto tableName(m_tableName);
				pageNames.append(tableName.insert(tableName.find('.'), std::to_string(i))).append(" ");
			}

			CString cmdLine(fmt::format("mutool.exe create -o {} {}", pdfFilePath, pageNames).c_str());
			auto nStrBuffer = cmdLine.GetLength() + 10;

			PROCESS_INFORMATION processInformation = { 0 };
			STARTUPINFO startupInfo = { 0 };
			startupInfo.cb = sizeof(startupInfo);
			BOOL result = CreateProcess(NULL, cmdLine.GetBuffer(nStrBuffer), NULL, NULL, FALSE,
				NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW, NULL, NULL, &startupInfo, &processInformation);
			cmdLine.ReleaseBuffer();

			if (!result) {
				// CreateProcess() failed
				// Get the error from the system
				LPVOID lpMsgBuf;
				DWORD dw = GetLastError();
				FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);

				// Display the error
				CString strError = (LPTSTR)lpMsgBuf;
				printf("::executeCommandLine() failed at CreateProcess()\nCommand=%s\nMessage=%s\n\n", cmdLine, strError);

				// Free resources created by the system
				LocalFree(lpMsgBuf);
			}
			else {
				// Successfully created the process.  Wait for it to finish.
				WaitForSingleObject(processInformation.hProcess, INFINITE);

				// ShellExecute(NULL, NULL, pdfFilePath.data(), NULL, NULL, SW_SHOWNORMAL);
				ShellExecute(NULL, NULL, pdfFilePath.data(), NULL, NULL, SW_SHOWNORMAL);

				// Close the handles.
				CloseHandle(processInformation.hProcess);
				CloseHandle(processInformation.hThread);
			}
		}

	public:
		// 文件操作接口
		void CreatePdfFile() {
			m_files.push_back(new lxd::File(Utf8ToUnicode(GetCurFileName()), lxd::WriteOnly | lxd::Truncate));
			SetCurFile(m_files.back());
			ResetBottom();

			const std::string initConfig("%%MediaBox 0 0 707 1000\r\n%%Font TmRm Times-Roman\r\n%%Font TmBd Times-Bold \r\n%%CJKFont Song zh-Hans\r\n%%CJKFont SnBd zh-Hans\r\n");
			m_currFile->write(initConfig.data(), initConfig.size());

			if (m_enableHeader) {
				ConfigHeader();
			}
		}

		std::string LoadImage(const std::string imagePath) {
			if (std::filesystem::exists(imagePath.data())) {
				auto imageData = fmt::format("%%Image I{} {}\r\n", m_imageIndex, imagePath);
				m_currFile->write(imageData.data(), imageData.size());
				return fmt::format("/I{}", m_imageIndex);
			}
			print({ fmt::format("Image path: {} not found\n", imagePath) });
			return std::string();
		}

		// TODO: 将传入参数修改为fdi-牙齿类型的键值对，根据病例只导入所需图片
		void LoadImageSet(TOOTH_IMAGE toothImage) {
			std::string prefix;
			switch (toothImage) {
			case TOOTH_IMAGE::NORMAL:
				prefix = "n";
				break;
			case TOOTH_IMAGE::ATTACHMENT:
				prefix = "at";
				break;
			case TOOTH_IMAGE::MISSING:
				prefix = "m";
				break;
			}

			for (int j = 1; j <= 4; j++) {
				for (int i = 1; i <= 8; i++) {
					auto fdi = j * 10 + i;
					auto imagePath = fmt::format("image/{}{}.png", prefix, fdi);
					LoadImage(imagePath);
					int32_t width = {}, height = {}, comp = {};
					stbi_info(imagePath.data(), &width, &height, &comp);
					auto imageInfo = ImageInfo();
					imageInfo.m_imageId = fmt::format("/I{}", m_imageIndex++);
					imageInfo.width = width;
					imageInfo.height = height;
					m_fdiMap.insert({ fdi, imageInfo });
				}
			}
		}

	public:
		// PDF元素绘制底层接口
		template<hasContent T>
		void Draw(const T& component) {}

		template<>
		void Draw(const Rect& component) {
			auto bottom = component.StartPosition().y - component.Size().y;
			const auto& content = component.Content();
			m_currFile->write(content.data(), content.size());

			if (component.m_type == Rect::Type::Block) {
				m_lastDrawPadding = component.Size().y;

				if (bottom < m_bottom) {
					m_bottom = bottom;
				}
			}
		}

		template<>
		void Draw(const Circle& component) {
			auto content = component.Content();
			m_currFile->write(content.data(), content.size());
		}

		template<>
		void Draw(const Streak& component) {
			auto bottom = component.StartPosition().y;
			const auto& content = component.Content();
			m_currFile->write(content.data(), content.size());

			m_lastDrawPadding = PDF_SECTION_PADDING;
			if (bottom < m_bottom) {
				m_bottom = bottom;
			}
		}

		template<>
		void Draw(const Image& component) {
			auto bottom = component.RealDrawPosition().y;
			const auto img = component.Content();
			m_currFile->write(img.data(), img.size());

			m_lastDrawPadding = component.GetDrawPadding();
			if (bottom < m_bottom) {
				m_bottom = bottom;
			}
		}

		template<>
		void Draw(const Text& component) {
			const_cast<Text*>(&component)->CalcLayout();
			auto textVec = component.GetContent();

			for (const auto& text : textVec) {
				m_currFile->write(text.data(), text.size());
				if (text != textVec.back()) {
					CreatePdfFile();
				}
			}

			m_lastDrawPadding = component.GetFontSize() + PDF_LINE_PADDING;
			if (component.GetBottom() < m_bottom) {
				m_bottom = component.GetBottom();
			}
		}

	public:
		// PDF表格元素插入接口
		void TextInsertion(const std::wstring_view wstr, float fontSize, Vector2 pos) {
			auto text = Text(wstr, fontSize, pos);
			Draw(text);
		}

		void TextInsertion(const std::wstring_view wstr, float fontSize, float depth, ALIGNMENT alignment) {
			auto text = Text(wstr, fontSize, { 0.0, depth });
			text.SetAlignment(alignment);
			Draw(text);
		}

		void TextInsertion(const std::wstring_view wstr, float fontSize, Vector2 pos, TextStyle styleInfo) {
			auto text = Text(wstr, fontSize, pos);
			text.SetAlignment(styleInfo.alignment, styleInfo.alignmentRange);
			Draw(text);
		}

		void TextInsertion(const std::wstring_view wstr, float fontSize, float depth, TextStyle styleInfo) {
			auto text = Text(wstr, fontSize, { 0.0, depth });
			text.SetAlignment(styleInfo.alignment, styleInfo.alignmentRange);
			Draw(text);
		}

		template<class T>
		void EntityInsertion(T entity) {
			Draw(entity);
		}

		void StreakInsertion() {
			auto bottom = GetNextLine();
			EntityInsertion(Streak(Vector2{ PDF_PADDING, bottom }, { PDF_WIDTH - PDF_PADDING, bottom }));
		}

		void RectInsertion() {
			float rectWidth = 20;
			EntityInsertion(Rect(Vector2{ PDF_PADDING, GetNextLine() - m_lastDrawPadding + rectWidth }, Vector2{ PDF_WIDTH - PDF_PADDING * 2, rectWidth }, Vector3{ 0.572, 0.815, 0.313 }, Rect::Type::Block));
		}

		void ImageInsertion(int32_t imageName, Vector2 startPosition, Vector2 size, Image::Direction drawDirection = Image::Direction::Upwards) {
			Draw(Image(GetImageId(imageName), startPosition, size, drawDirection));
		}

	public:
		// 取值接口
		float GetBottom() {
			return PDF_HEIGHT - m_bottom;
		}

		float GetNextLine(float extraPadding = 0.0) {
			auto line = PDF_HEIGHT - m_bottom + m_lastDrawPadding + extraPadding;

			if (line > PDF_BOTTOM) {
				if (m_enableFooter) {
					ConfigFooter();
				}

				CreatePdfFile();

				return PDF_PADDING;
			}

			if (FLOAT_EQUAL(line - extraPadding, 0.0))
				return PDF_PADDING;

			return line;
		}

		std::string_view GetImageId(int32_t imageName) {
			return m_fdiMap[imageName].m_imageId;
		}

		Vector2 GetImageSize(int32_t imageName) {
			return { static_cast<float>(m_fdiMap[imageName].width * m_fdiMap[imageName].scaling), static_cast<float>(m_fdiMap[imageName].height * m_fdiMap[imageName].scaling) };
		}

		ImageInfo GetImageInfo(int32_t imageName) {
			return m_fdiMap[imageName];
		}

		Vector2 GetIprInfoInsertionPosition(int32_t lFdi, int32_t rFdi) {
			auto area = lFdi / 10;
			if (area == 1 || area == 4)
				lFdi = (lFdi > rFdi) ? lFdi : rFdi;
			else
				lFdi = (lFdi < rFdi) ? lFdi : rFdi;

			auto x = m_fdiMap[lFdi].horzRange.y;
			auto y = m_fdiMap[lFdi].drawPosition.y;

			return { x, y };
		}

	public:
		void ConfigHeader() {

		}

		void ConfigFooter() {

		}

		// File Manipulation
	private:
		void ResetBottom() {
			m_bottom = PDF_HEIGHT;
		}

		void SetCurFile(lxd::File* file) { m_currFile = file; }
		std::string GetCurFileName() {
			auto res(m_tableName);
			return res.insert(res.find('.'), std::to_string(m_files.size()));
		}

	public:
		size_t m_bottom = PDF_HEIGHT;
	private:
		std::string m_tableName;
		lxd::File* m_currFile;
		std::vector<lxd::File*> m_files;
	private:
		float m_lastDrawPadding = {};
		float m_lastTextDrawLength = {};
		inline static size_t m_imageIndex = {};
		FIGURE m_lastDrawFigure = FIGURE::DEFAULT;
	private:
		std::map<int32_t, ImageInfo> m_fdiMap;
	private:
		bool m_enableHeader = true;
		bool m_enableFooter = true;
	private:
		inline static bool m_ctxInitialized = false;
	};

	void PDFTest2() {
		PDFTextTable table("TextCaption.txt");
		cxxtimer::Timer timer;

		// timer.start();
		// auto str = read_string_from_file("file.txt");
		// auto res = table.GetBottom();
		// auto text = Text{ Utf8ToUnicode(str), 12.0, table.GetNextLine(), ALIGNMENT::LEFT }.SetAutoNextPage(true);
		// table.Draw(text);
		// timer.stop();
		// std::cout << "pdf lib takes: " << timer.count<std::chrono::milliseconds>() << " milliseconds." << std::endl;
		// timer.reset();

		timer.start();
		table.GeneratePDF("CaptionFile");
		timer.stop();
		std::cout << "Mutool takes: " << timer.count<std::chrono::milliseconds>() << " milliseconds." << std::endl;
	}

	void PDFTest3() {
		PDFTextTable table("TextCaption.txt");

		// table.Draw(Image{ "image/n11.png", Position{200, 200}, 60 }
		// 	// .Append(Text().SetFontSize(12).Append(L"hello world").SetPosition(Position{ 100, 100 }))
		// .Append(Image{ "image/n11.png", Position{500, 500}, 60 }));

		table.Draw(Text().SetFontSize(12).Append(L"hello world").SetPosition(Position{ 100, 100 }));

		// table.Draw(Image{ "image/n11.png", Position{200, 200}, 10 });
		// table.Draw(Circle(Position{ 200, 200 }, 2, Color3{ 0.537, 0.058, 0.050 }));
		// table.TextInsertion(L"Hello World", 16, { 210, 200 });

		table.GeneratePDF("CaptionFile");
	}
}

void main() {
	// pdf::ExportCaptionTable();
	pdf::PDFTest3();
}
