/*
Sable, a native port of NightPDF
Copyright (C) 2021  Advaith Madhukar
Copyright (C) 2026  Abhilash Kar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include "document_internal.h"

namespace pdf {
using namespace detail;

Document* Document::formDrawingDocument() const {
	if (offThread() || FPDF_GetFormType(m_handle) != FORMTYPE_ACRO_FORM) {
		return nullptr;
	}
	if (m_formDrawingRevision != m_annotationRevision) {
		m_formDrawing.reset();
		m_formDrawingRevision = m_annotationRevision;
		try {
			if (auto bytes = serialize(128 * 1024 * 1024)) {
				m_formDrawing = reopenFormCopy(std::move(*bytes)).document;
			}
		} catch (...) {
			// i keep the base page when an isolated copy cannot be made.
		}
	}
	return m_formDrawing.get();
}

void Document::drawFormFields(int index, void* bitmap, int x, int y, int width,
                              int height, int rotation, bool printing) const {
	const auto scratch = formDrawingDocument();
	if (!scratch) {
		return;
	}
	FormContext context(scratch->m_handle, index);
	context.handle =
	    FPDFDOC_InitFormFillEnvironment(context.document, &context);
	if (!context.handle) {
		return;
	}
	context.page.reset(FPDF_LoadPage(context.document, index));
	if (!context.page) {
		return;
	}
	PagePtr source(FPDF_LoadPage(m_handle, index));
	const int count = FPDFPage_GetAnnotCount(context.page.get());
	if (!source || count < 0 || count > 4096 ||
	    count != FPDFPage_GetAnnotCount(source.get())) {
		return;
	}
	for (int i = 0; i < count; ++i) {
		AnnotationPtr original(FPDFPage_GetAnnot(source.get(), i));
		AnnotationPtr copy(FPDFPage_GetAnnot(context.page.get(), i));
		if (!original || !copy) {
			return;
		}
		if (FPDFAnnot_GetSubtype(original.get()) != FPDF_ANNOT_WIDGET) {
			continue;
		}
		const int flags = FPDFAnnot_GetFlags(original.get());
		const bool visible =
		    !(flags & (FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN)) &&
		    (!printing || !FPDFAnnot_HasKey(original.get(), "OC")) &&
		    (printing ? (flags & FPDF_ANNOT_FLAG_PRINT) != 0
		              : (flags & FPDF_ANNOT_FLAG_NOVIEW) == 0);
		// i reapply live flags each draw; a print mask cannot leak onscreen.
		// the pinned form renderer ignores FPDF_PRINTING. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_formfill.cpp
		if (!FPDFAnnot_SetFlags(copy.get(),
		                        visible ? flags & ~FPDF_ANNOT_FLAG_NOVIEW
		                                : flags | FPDF_ANNOT_FLAG_HIDDEN)) {
			return;
		}
	}
	context.afterLoad();
	// i draw widgets after page content with the same transform and flags.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/public/fpdf_formfill.h
	FPDF_FFLDraw(context.handle, static_cast<FPDF_BITMAP>(bitmap),
	             context.page.get(), x, y, width, height, rotation,
	             FPDF_ANNOT | (printing ? FPDF_PRINTING : 0));
}

bool Document::canFillForms() const {
	if (offThread()) {
		return false;
	}
	const auto permissions = FPDF_GetDocPermissions(m_handle);
	// table 22 reserves bit 9 before revision 3. see:
	// https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf
	return (permissions & 0x20) ||
	       (FPDF_GetSecurityHandlerRevision(m_handle) >= 3 &&
	        (permissions & 0x100));
}

std::optional<bool>
Document::updateTextField(int pageIndex, int index,
                          std::u16string_view expected,
                          std::u16string_view contents, std::uint64_t revision,
                          const std::function<bool()>& canceled) {
	return updateFormField(pageIndex, index, expected, contents, revision,
	                       canceled, {});
}

std::optional<bool>
Document::updateCheckBox(int pageIndex, int index, std::u16string_view expected,
                         bool checked, std::uint64_t revision,
                         const std::function<bool()>& canceled) {
	return updateFormField(pageIndex, index, expected, {}, revision, canceled,
	                       checked);
}

std::optional<bool>
Document::updateRadioButton(int pageIndex, int index,
                            std::u16string_view expected,
                            const FormControl& control, std::uint64_t revision,
                            const std::function<bool()>& canceled) {
	return updateFormField(pageIndex, index, expected, {}, revision, canceled,
	                       true, &control);
}

std::optional<bool>
Document::updateChoiceField(int page, const FormField& field, int option,
                            std::uint64_t revision,
                            const std::function<bool()>& canceled) {
	if ((field.type != FormFieldType::ComboBox &&
	     field.type != FormFieldType::ListBox) ||
	    !field.options || field.options->empty() ||
	    field.options->size() > 256 || option < -1 ||
	    (option == -1 && field.type != FormFieldType::ListBox) ||
	    (option >= 0 &&
	     static_cast<std::size_t>(option) >= field.options->size()) ||
	    std::any_of(
	        field.options->begin(), field.options->end(),
	        [](const auto& item) { return item.label.empty(); })) {
		return {};
	}
	return updateFormField(page, field.index, {}, {}, revision, canceled, {},
	                       nullptr, &field, option);
}

std::optional<bool> Document::updateFormField(
    int pageIndex, int index, std::u16string_view expected,
    std::u16string_view contents, std::uint64_t revision,
    const std::function<bool()>& canceled, std::optional<bool> checked,
    const FormControl* control, const FormField* choice, int choiceIndex) {
	const auto stopped = [&] { return canceled && canceled(); };
	const bool clearingChoice = choice && choiceIndex == -1;
	const auto type = choice    ? choice->type
	                  : control ? FormFieldType::RadioButton
	                  : checked ? FormFieldType::CheckBox
	                            : FormFieldType::Text;
	if (offThread() || !revision || !currentRevision(revision) ||
	    (control &&
	     (control->count < 1 || control->count > 256 || control->index < 0 ||
	      control->index >= control->count || !checked || !*checked)) ||
	    pageIndex < 0 || pageIndex >= pageCount() || index < 0 ||
	    !canFillForms() || !validNoteText(expected) ||
	    !validNoteText(contents) || stopped() ||
	    canWrite(canceled) != WriteRefusal::None ||
	    !canSerializeEdits(canceled)) {
		return {};
	}
	auto scratch = transaction(canceled);
	if (!scratch.document ||
	    FPDF_GetFormType(scratch.document->m_handle) != FORMTYPE_ACRO_FORM ||
	    !scratch.canonicalize()) {
		return {};
	}
	scratch.document->invalidateWriteCopy();
	// a full rewrite expands object streams and writes decoded
	// dictionary/name tokens. canWrite already refused signatures. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/edit/cpdf_creator.cpp
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/parser/cpdf_name.cpp
	const std::string_view tokens(
	    reinterpret_cast<const char*>(scratch.document->m_data.data()),
	    scratch.document->m_data.size());
	// i can't see every merged field's actions through its widget.
	if (formActionTokens(tokens)) {
		return {};
	}

	const auto matchesChoice = [&](const FormField& field, bool after) {
		return field.index == index && field.type == type &&
		       field.name == choice->name && field.flags == choice->flags &&
		       field.annotationFlags == choice->annotationFlags &&
		       field.options &&
		       field.options->size() == choice->options->size() &&
		       std::equal(
		           field.options->begin(), field.options->end(),
		           choice->options->begin(), [&](const auto& a, const auto& b) {
			           return a.index == b.index && a.label == b.label &&
			                  a.selected ==
			                      (after ? a.index == choiceIndex : b.selected);
		           });
	};
	if (choice) {
		const auto before = scratch.document->formFields(pageIndex, canceled);
		if (!before || before->limited ||
		    std::none_of(before->entries.begin(), before->entries.end(),
		                 [&](const auto& field) {
			                 return matchesChoice(field, false) &&
			                        field.value == choice->value;
		                 })) {
			return {};
		}
	}
	// group members by position, kept for the reopened check.
	std::vector<int> members(control ? control->count : 0, -1);
	std::vector<std::u16string> exports(members.size());
	std::u16string replacement(contents);
	std::optional<FormField> inheritedTarget;
	constexpr int plainFlags = FPDF_FORMFLAG_REQUIRED | FPDF_FORMFLAG_NOEXPORT;
	// i keep DoNotSpellCheck on text fields. see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/constants/form_flags.h
	constexpr int textPlainFlags = plainFlags | (1 << 22);
	// Sort is for writers; i keep the stored option order. see:
	// https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf
	constexpr int choiceFlags = 1 << 19;
	const auto clearedChoice = [](FPDF_ANNOTATION widget) {
		return widget && !FPDFAnnot_HasKey(widget, "Parent") &&
		       !FPDFAnnot_HasKey(widget, "DV") &&
		       !FPDFAnnot_HasKey(widget, "V") && !FPDFAnnot_HasKey(widget, "I");
	};
	const auto inheritedWidget = [&](FPDF_FORMHANDLE form,
	                                 FPDF_ANNOTATION widget) {
		// i require the actual widget in the resolved field, not just its
		// name. count one also bounds appearance propagation. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
		if (!widget || !FPDFAnnot_HasKey(widget, "Parent") ||
		    FPDFAnnot_GetFormFieldType(form, widget) !=
		        static_cast<int>(FormFieldType::Text) ||
		    FPDFAnnot_GetFormControlCount(form, widget) != 1 ||
		    FPDFAnnot_GetFormControlIndex(form, widget) != 0 ||
		    (FPDFAnnot_GetFormFieldFlags(form, widget) &
		     ~(textPlainFlags | FPDF_FORMFLAG_TEXT_MULTILINE))) {
			return false;
		}
		// i leave field data on the parent of this plain widget. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_interactiveform.cpp
		for (const auto key :
		     {"T", "FT", "Ff", "V", "DV", "Kids", "MaxLen", "RV", "DA", "Q",
		      "DS", "TU", "TM", "Opt", "I", "TI"}) {
			if (FPDFAnnot_HasKey(widget, key)) {
				return false;
			}
		}
		AnnotationPtr parent(FPDFAnnot_GetLinkedAnnot(widget, "Parent"));
		return !parent;
	};
	{
		FormContext context(scratch.document->m_handle, pageIndex);
		context.handle =
		    FPDFDOC_InitFormFillEnvironment(context.document, &context);
		if (!context.handle) {
			return {};
		}
		context.page.reset(FPDF_LoadPage(context.document, pageIndex));
		if (!context.page ||
		    index >= FPDFPage_GetAnnotCount(context.page.get())) {
			return {};
		}
		context.afterLoad();
		AnnotationPtr field(FPDFPage_GetAnnot(context.page.get(), index));
		// i keep one control unless this is an inspected radio group.
		// the pinned control-count implementation also handles text fields.
		// see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
		constexpr int protectedFlags =
		    FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN |
		    FPDF_ANNOT_FLAG_NOVIEW | FPDF_ANNOT_FLAG_READONLY |
		    FPDF_ANNOT_FLAG_LOCKED | 0x200;
		// radio flags have no SDK names: NoToggleToOff and Radio. unison
		// groups and the other button flags stay refused.
		constexpr int radioFlags = (1 << 14) | (1 << 15);
		const auto plainWidget = [&](FPDF_ANNOTATION annotation) {
			return FPDFAnnot_GetSubtype(annotation) == FPDF_ANNOT_WIDGET &&
			       !FPDFAnnot_HasKey(annotation, "A") &&
			       !FPDFAnnot_HasKey(annotation, "AA") &&
			       !(FPDFAnnot_GetFlags(annotation) & protectedFlags);
		};
		if (!field || !plainWidget(field.get()) ||
		    FPDFAnnot_GetFormFieldType(context.handle, field.get()) !=
		        static_cast<int>(type) ||
		    FPDFAnnot_GetFormControlCount(context.handle, field.get()) !=
		        (control ? control->count : 1) ||
		    (!control && type != FormFieldType::Text &&
		     FPDFAnnot_HasKey(field.get(), "Parent"))) {
			return {};
		}
		if (type == FormFieldType::Text &&
		    FPDFAnnot_HasKey(field.get(), "Parent")) {
			if (!inheritedWidget(context.handle, field.get())) {
				return {};
			}
			const auto fields =
			    scratch.document->formFields(pageIndex, canceled);
			if (!fields || fields->limited) {
				return {};
			}
			const auto target = std::find_if(
			    fields->entries.begin(), fields->entries.end(),
			    [index](const auto& item) { return item.index == index; });
			if (target == fields->entries.end() || target->name.empty() ||
			    std::count_if(fields->entries.begin(), fields->entries.end(),
			                  [&](const auto& item) {
				                  return item.name == target->name;
			                  }) != 1) {
				return {};
			}
			inheritedTarget = *target;
		}
		const int flags =
		    FPDFAnnot_GetFormFieldFlags(context.handle, field.get());
		const bool multiline = type == FormFieldType::Text &&
		                       (flags & FPDF_FORMFLAG_TEXT_MULTILINE);
		// the public header has no Comb constant. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/constants/form_flags.h
		constexpr int combFlag = 1 << 24;
		const bool comb = type == FormFieldType::Text && (flags & combFlag);
		if ((flags &
		     ~((type == FormFieldType::Text ? textPlainFlags : plainFlags) |
		       (control     ? radioFlags
		        : choice    ? choiceFlags | (type == FormFieldType::ComboBox
		                                         ? FPDF_FORMFLAG_CHOICE_COMBO
		                                         : 0)
		        : multiline ? FPDF_FORMFLAG_TEXT_MULTILINE
		        : comb      ? combFlag
		                    : 0))) ||
		    (!multiline &&
		     contents.find_first_of(u"\r\n") != std::u16string_view::npos)) {
			return {};
		}
		if (comb) {
			// i require a bounded cell count before opening the comb editor.
			// see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_textfield.cpp
			float cells = 0;
			if (!FPDFAnnot_GetNumberValue(field.get(), "MaxLen", &cells) ||
			    !std::isfinite(cells) || cells < 1 || cells > 32767 ||
			    std::floor(cells) != cells) {
				return {};
			}
		}
		const auto read = [&](FPDF_ANNOTATION annotation,
		                      auto getter) -> std::optional<std::u16string> {
			const auto size = getter(context.handle, annotation, nullptr, 0);
			if (size < 2 || size > 65536 || size % 2) {
				return {};
			}
			std::vector<FPDF_WCHAR> buffer(size / 2);
			if (getter(context.handle, annotation, buffer.data(), size) !=
			        size ||
			    buffer.back() != 0) {
				return {};
			}
			return std::u16string(buffer.begin(), buffer.end() - 1);
		};
		const auto value = [&](auto getter) {
			return read(field.get(), getter);
		};
		const auto previous = value(FPDFAnnot_GetFormFieldValue);
		const auto namedOf = [&](FPDF_ANNOTATION target, const char* key) {
			return read(target, [key](FPDF_FORMHANDLE,
			                          FPDF_ANNOTATION annotation,
			                          FPDF_WCHAR* buffer, unsigned long size) {
				return FPDFAnnot_GetStringValue(annotation, key, buffer, size);
			});
		};
		const auto named = [&](const char* key) {
			return namedOf(field.get(), key);
		};
		const auto checkedAppearance = [&](FPDF_ANNOTATION target) {
			// i keep checked appearances to text; this SDK can return
			// compressed bytes for empty streams. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfapi/parser/cpdf_stream_acc.cpp
			const auto appearance =
			    read(target, [](FPDF_FORMHANDLE, FPDF_ANNOTATION annotation,
			                    FPDF_WCHAR* buffer, unsigned long size) {
				    return FPDFAnnot_GetAP(annotation,
				                           FPDF_ANNOT_APPEARANCEMODE_NORMAL,
				                           buffer, size);
			    });
			const std::u16string_view whitespace(u"\0\t\n\f\r ", 6);
			return appearance &&
			       appearance->find_first_not_of(whitespace) !=
			           std::u16string::npos &&
			       std::all_of(appearance->begin(), appearance->end(),
			                   [&](char16_t unit) {
				                   return (unit >= 32 && unit <= 126) ||
				                          whitespace.find(unit) !=
				                              std::u16string_view::npos;
			                   });
		};
		const auto hasCheckedAppearance = [&] {
			return checkedAppearance(field.get());
		};
		const bool terminal =
		    !control || !FPDFAnnot_HasKey(field.get(), "Parent");
		if (!previous ||
		    (choice ? utf16ToUtf8(*previous) != choice->value
		            : *previous != expected) ||
		    stopped()) {
			return {};
		}
		if (choice) {
			const auto selectedCount =
			    std::count_if(choice->options->begin(), choice->options->end(),
			                  [](const auto& item) { return item.selected; });
			if (!validNoteText(*previous) || selectedCount > 1 ||
			    (previous->empty() ? selectedCount != 0 : selectedCount != 1) ||
			    (clearingChoice && FPDFAnnot_HasKey(field.get(), "DV")) ||
			    FPDFAnnot_HasKey(field.get(), "Kids") || named("FT") != u"Ch" ||
			    FPDFAnnot_GetValueType(field.get(), "Opt") !=
			        FPDF_OBJECT_ARRAY ||
			    (FPDFAnnot_HasKey(field.get(), "V") &&
			     (FPDFAnnot_GetValueType(field.get(), "V") !=
			          FPDF_OBJECT_STRING ||
			      named("V") != previous))) {
				return {};
			}
		}
		if (checked && !control) {
			const auto on = value(FPDFAnnot_GetFormFieldExportValue);
			const bool current =
			    FPDFAnnot_IsChecked(context.handle, field.get());
			// i keep the value and appearance state on the same export name.
			if (!on || on->empty() || *on == u"Off" || !validNoteText(*on) ||
			    named("AS") != previous || named("V") != previous ||
			    *previous != (current ? *on : u"Off") ||
			    FPDFAnnot_GetValueType(field.get(), "V") != FPDF_OBJECT_NAME ||
			    FPDFAnnot_GetValueType(field.get(), "AS") != FPDF_OBJECT_NAME ||
			    FPDFAnnot_HasKey(field.get(), "Opt") ||
			    (current && !hasCheckedAppearance())) {
				return {};
			}
			replacement = *checked ? *on : u"Off";
		}
		if (control) {
			// i only write a group whose members are all on this page once;
			// otherwise clearing the old choice could reach a widget i never
			// checked.
			const auto name = value(FPDFAnnot_GetFormFieldName);
			const int count = FPDFPage_GetAnnotCount(context.page.get());
			if (!name || count < 0 || count > 4096 ||
			    (!FPDFAnnot_HasKey(field.get(), "Parent") &&
			     (control->count != 1 || FPDFAnnot_HasKey(field.get(), "Opt") ||
			      FPDFAnnot_GetValueType(field.get(), "V") !=
			          FPDF_OBJECT_NAME ||
			      named("V") != previous))) {
				return {};
			}
			// a parent field can hold scripts the linked-annotation API
			// can't open, but the form API reports them through inheritance.
			for (int event = FPDF_ANNOT_AACTION_KEY_STROKE;
			     event <= FPDF_ANNOT_AACTION_CALCULATE; ++event) {
				if (FPDFAnnot_GetFormAdditionalActionJavaScript(
				        context.handle, field.get(), event, nullptr, 0) != 2) {
					return {};
				}
			}
			std::optional<int> selected;
			for (int i = 0; i < count; ++i) {
				AnnotationPtr member(FPDFPage_GetAnnot(context.page.get(), i));
				if (!member || stopped()) {
					return {};
				}
				if (FPDFAnnot_GetSubtype(member.get()) != FPDF_ANNOT_WIDGET ||
				    FPDFAnnot_GetFormFieldType(context.handle, member.get()) !=
				        static_cast<int>(type) ||
				    read(member.get(), FPDFAnnot_GetFormFieldName) != name) {
					continue;
				}
				const int position =
				    FPDFAnnot_GetFormControlIndex(context.handle, member.get());
				const auto exported =
				    read(member.get(), FPDFAnnot_GetFormFieldExportValue);
				const auto state = namedOf(member.get(), "AS");
				const bool on =
				    FPDFAnnot_IsChecked(context.handle, member.get()) != 0;
				// duplicate exports can't say which control V selects.
				if (!plainWidget(member.get()) ||
				    FPDFAnnot_GetFormControlCount(
				        context.handle, member.get()) != control->count ||
				    position < 0 || position >= control->count ||
				    members[position] != -1 || !exported || exported->empty() ||
				    *exported == u"Off" || !validNoteText(*exported) ||
				    std::find(exports.begin(), exports.end(), *exported) !=
				        exports.end() ||
				    FPDFAnnot_GetValueType(member.get(), "AS") !=
				        FPDF_OBJECT_NAME ||
				    !state || (*state == u"Off") == on ||
				    (on && (selected || !checkedAppearance(member.get())))) {
					return {};
				}
				if (FPDFAnnot_HasKey(member.get(), "Parent")) {
					// kids stay plain widgets, not fields of their own.
					for (const char* key :
					     {"T", "FT", "Ff", "V", "DV", "Opt", "Kids"}) {
						if (FPDFAnnot_HasKey(member.get(), key)) {
							return {};
						}
					}
				} else if (control->count != 1) {
					return {};
				}
				members[position] = i;
				exports[position] = *exported;
				if (on) {
					selected = position;
				}
			}
			if (std::find(members.begin(), members.end(), -1) !=
			        members.end() ||
			    members[control->index] != index ||
			    utf16ToUtf8(exports[control->index]) != control->exportValue ||
			    *previous !=
			        (selected ? exports[*selected] : std::u16string(u"Off"))) {
				return {};
			}
			replacement = exports[control->index];
		}
		if (multiline && *previous != replacement) {
			// i keep raw no-ops, but match the editor's CRLF paragraphs.
			// see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/pwl/cpwl_edit_impl.cpp
			std::u16string normalized;
			normalized.reserve(replacement.size());
			for (std::size_t i = 0; i < replacement.size(); ++i) {
				const auto unit = replacement[i];
				if (unit == u'\r' || unit == u'\n') {
					normalized += u"\r\n";
					if (unit == u'\r' && i + 1 < replacement.size() &&
					    replacement[i + 1] == u'\n') {
						++i;
					}
				} else {
					normalized += unit;
				}
			}
			if (!validNoteText(normalized)) {
				return {};
			}
			replacement = std::move(normalized);
		}
		if (choice ? (clearingChoice ? previous->empty()
		                             : (*choice->options)[choiceIndex].selected)
		           : *previous == replacement) {
			return false;
		}
		// i commit through focus loss, then check the full value. see:
		// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_textfield.cpp
		if (!FORM_SetFocusedAnnot(context.handle, field.get())) {
			return {};
		}
		if (choice) {
			// i save the focused choice through focus loss too. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_combobox.cpp
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_listbox.cpp
			int selected = choiceIndex;
			if (clearingChoice) {
				const auto found = std::find_if(
				    choice->options->begin(), choice->options->end(),
				    [](const auto& item) { return item.selected; });
				if (found == choice->options->end()) {
					return {};
				}
				selected = found->index;
			}
			if (!FORM_SetIndexSelected(context.handle, context.page.get(),
			                           selected, !clearingChoice)) {
				return {};
			}
		} else if (checked) {
			// space commits the focused checkbox. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/formfiller/cffl_checkbox.cpp
			if (!FORM_OnChar(context.handle, context.page.get(), ' ', 0)) {
				return {};
			}
		} else {
			if (!previous->empty() &&
			    !FORM_SelectAllText(context.handle, context.page.get())) {
				return {};
			}
			FORM_ReplaceSelection(
			    context.handle, context.page.get(),
			    reinterpret_cast<FPDF_WIDESTRING>(replacement.c_str()));
		}
		if (!FORM_ForceToKillFocus(context.handle)) {
			return {};
		}
		if (choice) {
			const auto written = value(FPDFAnnot_GetFormFieldValue);
			// clearing removes V/I; DV would become the effective value.
			// see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/core/fpdfdoc/cpdf_formfield.cpp
			if (!written || !validNoteText(*written) ||
			    (clearingChoice
			         ? !written->empty() || !clearedChoice(field.get())
			         : written->empty() || named("V") != written ||
			               FPDFAnnot_GetValueType(field.get(), "V") !=
			                   FPDF_OBJECT_STRING) ||
			    FPDFAnnot_GetOptionCount(context.handle, field.get()) !=
			        static_cast<int>(choice->options->size())) {
				return {};
			}
			replacement = *written;
			for (const auto& item : *choice->options) {
				if ((FPDFAnnot_IsOptionSelected(context.handle, field.get(),
				                                item.index) != 0) !=
				        (item.index == choiceIndex) ||
				    stopped()) {
					return {};
				}
			}
		}
		if (value(FPDFAnnot_GetFormFieldValue) != replacement ||
		    (checked && terminal &&
		     (named("V") != replacement || named("AS") != replacement)) ||
		    (checked && (FPDFAnnot_IsChecked(context.handle, field.get()) !=
		                 0) != *checked) ||
		    (checked && *checked && !hasCheckedAppearance()) || stopped()) {
			return {};
		}
		if (control) {
			for (int position = 0; position < control->count; ++position) {
				AnnotationPtr member(
				    FPDFPage_GetAnnot(context.page.get(), members[position]));
				const bool on = position == control->index;
				const auto state =
				    member ? namedOf(member.get(), "AS") : std::nullopt;
				if (!state || (*state == u"Off") == on ||
				    (FPDFAnnot_IsChecked(context.handle, member.get()) != 0) !=
				        on ||
				    stopped()) {
					return {};
				}
			}
		}
	}
	// i commit only saved, reopened bytes; failed edits never touch the live
	// PDF.
	auto next = scratch.save();
	if (!next.document) {
		return {};
	}
	if (inheritedTarget || clearingChoice) {
		// i check saved ownership and cleared keys on another copy,
		// keeping setup off live bytes.
		auto proof = reopenFormCopy(next.document->m_data);
		if (!proof.document || stopped()) {
			return {};
		}
		FormContext context(proof.document->m_handle, pageIndex);
		context.handle =
		    FPDFDOC_InitFormFillEnvironment(context.document, &context);
		if (!context.handle) {
			return {};
		}
		context.page.reset(FPDF_LoadPage(context.document, pageIndex));
		if (!context.page) {
			return {};
		}
		context.afterLoad();
		AnnotationPtr field(FPDFPage_GetAnnot(context.page.get(), index));
		if ((inheritedTarget &&
		     !inheritedWidget(context.handle, field.get())) ||
		    (clearingChoice && !clearedChoice(field.get())) || stopped()) {
			return {};
		}
	}
	const auto check = next.document->formFields(pageIndex, canceled);
	if (!check || check->limited ||
	    std::none_of(check->entries.begin(), check->entries.end(),
	                 [&](const FormField& field) {
		                 return field.index == index && field.type == type &&
		                        (!checked || field.checked == checked) &&
		                        (!choice || matchesChoice(field, true)) &&
		                        (!inheritedTarget ||
		                         (field.name == inheritedTarget->name &&
		                          field.flags == inheritedTarget->flags &&
		                          field.annotationFlags ==
		                              inheritedTarget->annotationFlags)) &&
		                        field.value == utf16ToUtf8(replacement);
	                 })) {
		return {};
	}
	if (control) {
		const auto shared = utf16ToUtf8(replacement);
		for (int position = 0; position < control->count; ++position) {
			if (stopped()) {
				return {};
			}
			const auto exported = utf16ToUtf8(exports[position]);
			if (std::none_of(
			        check->entries.begin(), check->entries.end(),
			        [&](const FormField& field) {
				        return field.index == members[position] &&
				               field.type == type &&
				               field.checked == (position == control->index) &&
				               field.value == shared && field.control &&
				               field.control->index == position &&
				               field.control->count == control->count &&
				               field.control->exportValue == exported;
			        })) {
				return {};
			}
		}
	}
	if (!scratch.commit(std::move(next.document))) {
		return {};
	}
	return true;
}

namespace {
// i keep these flags specific to text fields. see:
// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/constants/form_flags.h
constexpr int noSpellCheckFlag = 1 << 22, combFlag = 1 << 24;
// i preserve the stored option order for Sort fields. see:
// https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf
constexpr int sortChoiceFlag = 1 << 19;
} // namespace

bool FormField::singleChoice() const {
	if ((type != FormFieldType::ComboBox && type != FormFieldType::ListBox) ||
	    !options || options->empty() || options->size() > 256) {
		return false;
	}
	int selected = 0;
	for (std::size_t i = 0; i < options->size(); ++i) {
		const auto& option = (*options)[i];
		if (option.index != static_cast<int>(i) || option.label.empty()) {
			return false;
		}
		selected += option.selected ? 1 : 0;
	}
	return selected <= 1;
}

bool FormField::multiline() const {
	return type == FormFieldType::Text &&
	       (flags & FPDF_FORMFLAG_TEXT_MULTILINE);
}

bool FormField::noSpellCheck() const {
	return type == FormFieldType::Text && (flags & noSpellCheckFlag);
}

bool FormField::supportsEditor() const {
	const bool text = type == FormFieldType::Text;
	const bool selection = singleChoice();
	const bool radio =
	    type == FormFieldType::RadioButton && control.has_value();
	if (!text && !selection &&
	    !((type == FormFieldType::CheckBox || radio) && checked.has_value())) {
		return false;
	}
	constexpr int plain = FPDF_FORMFLAG_REQUIRED | FPDF_FORMFLAG_NOEXPORT;
	constexpr int locked = FPDF_ANNOT_FLAG_READONLY | FPDF_ANNOT_FLAG_LOCKED |
	                       FPDF_ANNOT_FLAG_NOZOOM | FPDF_ANNOT_FLAG_NOROTATE |
	                       FPDF_ANNOT_FLAG_TOGGLENOVIEW | 0x200;
	int allowed = plain;
	if (radio) {
		// i allow Radio and NoToggleToOff for radio fields.
		allowed |= (1 << 14) | (1 << 15);
	} else if (selection) {
		allowed |= sortChoiceFlag;
		if (type == FormFieldType::ComboBox) {
			allowed |= FPDF_FORMFLAG_CHOICE_COMBO;
		}
	} else if (text) {
		// i keep comb and multiline flags exclusive.
		allowed |= noSpellCheckFlag |
		           (multiline() ? FPDF_FORMFLAG_TEXT_MULTILINE : combFlag);
	}
	return !(flags & ~allowed) && !(annotationFlags & locked);
}

std::optional<FormFields>
Document::formFields(int index, const std::function<bool()>& canceled) const {
	const auto stopped = [&] { return canceled && canceled(); };
	if (offThread() || index < 0 || index >= pageCount() || stopped()) {
		return {};
	}
	FormFields result;
	result.page = index;
	result.revision = m_annotationRevision;
	const int formType = FPDF_GetFormType(m_handle);
	if (formType == FORMTYPE_NONE) {
		return stopped() ? std::nullopt : std::optional{std::move(result)};
	}
	if (formType != FORMTYPE_ACRO_FORM) {
		return {};
	}
	// form setup can rewrite dictionaries and appearances, even without input.
	// see:
	// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/cpdfsdk_pageview.cpp
	auto bytes = serialize(128 * 1024 * 1024, canceled);
	if (!bytes || stopped()) {
		return {};
	}
	const auto scratch = reopenFormCopy(std::move(*bytes));
	if (!scratch.document || stopped()) {
		return {};
	}
	FormContext context(scratch.document->m_handle, index);
	context.handle =
	    FPDFDOC_InitFormFillEnvironment(context.document, &context);
	if (!context.handle) {
		return {};
	}
	context.page.reset(FPDF_LoadPage(context.document, index));
	if (!context.page) {
		return {};
	}
	context.afterLoad();
	const int count = FPDFPage_GetAnnotCount(context.page.get());
	if (count < 0) {
		return {};
	}
	result.limited = count > 4096;
	std::size_t total = 0;
	int optionTotal = 0;
	FS_RECTF pageBox{};
	const float width = FPDF_GetPageWidthF(context.page.get());
	const float height = FPDF_GetPageHeightF(context.page.get());
	const bool usablePage =
	    std::isfinite(width) && width > 0 && std::isfinite(height) &&
	    height > 0 && FPDF_GetPageBoundingBox(context.page.get(), &pageBox) &&
	    std::isfinite(pageBox.left) && std::isfinite(pageBox.right) &&
	    std::isfinite(pageBox.bottom) && std::isfinite(pageBox.top) &&
	    pageBox.left < pageBox.right && pageBox.bottom < pageBox.top;
	result.limited = result.limited || !usablePage;
	const auto displayed =
	    [&](const FS_RECTF& rect) -> std::optional<AnnotationRect> {
		if (!usablePage) {
			result.limited = true;
			return {};
		}
		// i clip in PDF coordinates before asking for integer device points.
		const double left = (std::max)(rect.left, pageBox.left);
		const double right = (std::min)(rect.right, pageBox.right);
		const double bottom = (std::max)(rect.bottom, pageBox.bottom);
		const double top = (std::min)(rect.top, pageBox.top);
		if (left >= right || bottom >= top) {
			return {};
		}
		constexpr int grid = 1 << 20;
		int ax = 0, ay = 0, bx = 0, by = 0;
		if (!FPDF_PageToDevice(context.page.get(), 0, 0, grid, grid, 0, left,
		                       bottom, &ax, &ay) ||
		    !FPDF_PageToDevice(context.page.get(), 0, 0, grid, grid, 0, right,
		                       top, &bx, &by) ||
		    ax < -1 || ay < -1 || bx < -1 || by < -1 || ax > grid + 1 ||
		    ay > grid + 1 || bx > grid + 1 || by > grid + 1) {
			result.limited = true;
			return {};
		}
		AnnotationRect shown{
		    std::clamp((std::min)(ax, bx) / double(grid), 0., 1.),
		    std::clamp((std::min)(ay, by) / double(grid), 0., 1.),
		    std::clamp((std::max)(ax, bx) / double(grid), 0., 1.),
		    std::clamp((std::max)(ay, by) / double(grid), 0., 1.)};
		if (shown.left >= shown.right || shown.top >= shown.bottom) {
			result.limited = true;
			return {};
		}
		return shown;
	};
	const auto text =
	    [&](auto getter,
	        FPDF_ANNOTATION annotation) -> std::optional<std::string> {
		const auto size = getter(context.handle, annotation, nullptr, 0);
		if (size < 2 || size % 2 || size > 64 * 1024 ||
		    total + size > 1024 * 1024) {
			result.limited = true;
			return {};
		}
		total += size;
		std::vector<unsigned char> buffer(size);
		if (getter(context.handle, annotation,
		           reinterpret_cast<FPDF_WCHAR*>(buffer.data()),
		           size) != size ||
		    buffer[size - 1] || buffer[size - 2]) {
			result.limited = true;
			return {};
		}
		buffer.resize(size - 2);
		return utf16ToUtf8(buffer);
	};
	static_assert(static_cast<int>(FormFieldType::Signature) ==
	              FPDF_FORMFIELD_SIGNATURE);
	for (int i = 0; i < (std::min)(count, 4096); ++i) {
		if (stopped()) {
			return {};
		}
		AnnotationPtr annotation(FPDFPage_GetAnnot(context.page.get(), i));
		if (!annotation) {
			result.limited = true;
			continue;
		}
		if (FPDFAnnot_GetSubtype(annotation.get()) != FPDF_ANNOT_WIDGET) {
			continue;
		}
		const int type =
		    FPDFAnnot_GetFormFieldType(context.handle, annotation.get());
		if (type < 0 || type > FPDF_FORMFIELD_SIGNATURE) {
			result.limited = true;
			continue;
		}
		FormField field;
		field.index = i;
		field.type = static_cast<FormFieldType>(type);
		field.flags =
		    FPDFAnnot_GetFormFieldFlags(context.handle, annotation.get());
		field.annotationFlags = FPDFAnnot_GetFlags(annotation.get());
		if (field.flags & FPDF_FORMFLAG_CHOICE_MULTI_SELECT) {
			result.limited = true;
		}
		field.name =
		    text(FPDFAnnot_GetFormFieldName, annotation.get()).value_or("");
		field.value =
		    text(FPDFAnnot_GetFormFieldValue, annotation.get()).value_or("");
		if (field.type == FormFieldType::CheckBox ||
		    field.type == FormFieldType::RadioButton) {
			field.checked =
			    FPDFAnnot_IsChecked(context.handle, annotation.get()) != 0;
			// i keep exports separate from the shared value, especially for
			// radio groups with Opt arrays. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/fpdf_annot.cpp
			const int controls =
			    FPDFAnnot_GetFormControlCount(context.handle, annotation.get());
			if (controls <= 0 || controls > 4096) {
				result.limited = true;
			} else {
				const int position = FPDFAnnot_GetFormControlIndex(
				    context.handle, annotation.get());
				if (position < 0 || position >= controls) {
					result.limited = true;
				} else if (auto exported =
				               text(FPDFAnnot_GetFormFieldExportValue,
				                    annotation.get())) {
					field.control =
					    FormControl{position, controls, std::move(*exported)};
				}
			}
		}
		if (field.type == FormFieldType::ComboBox ||
		    field.type == FormFieldType::ListBox) {
			const int options =
			    FPDFAnnot_GetOptionCount(context.handle, annotation.get());
			if (options < 0 || options > 4096 - optionTotal) {
				result.limited = true;
			} else {
				optionTotal += options;
				std::vector<FormOption> choices;
				choices.reserve(options);
				for (int option = 0; option < options; ++option) {
					if (stopped()) {
						return {};
					}
					auto label = text(
					    [option](auto handle, auto field, auto buffer,
					             auto size) {
						    return FPDFAnnot_GetOptionLabel(
						        handle, field, option, buffer, size);
					    },
					    annotation.get());
					if (!label) {
						break;
					}
					choices.push_back(
					    {option, std::move(*label),
					     FPDFAnnot_IsOptionSelected(
					         context.handle, annotation.get(), option) != 0});
				}
				if (choices.size() == static_cast<std::size_t>(options)) {
					field.options = std::move(choices);
				}
			}
		}
		FS_RECTF rect{};
		if (FPDFAnnot_GetValueType(annotation.get(), "Rect") ==
		        FPDF_OBJECT_ARRAY &&
		    FPDFAnnot_GetRect(annotation.get(), &rect) &&
		    std::isfinite(rect.left) && std::isfinite(rect.right) &&
		    std::isfinite(rect.bottom) && std::isfinite(rect.top) &&
		    rect.left < rect.right && rect.bottom < rect.top) {
			field.bounds =
			    AnnotationBounds{rect.left, rect.bottom, rect.right, rect.top};
			// i follow the SDK's visibility rule before exposing control
			// bounds. see:
			// https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8086/fpdfsdk/cpdfsdk_baannot.cpp
			if (!(field.annotationFlags &
			      (FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN |
			       FPDF_ANNOT_FLAG_NOVIEW))) {
				field.displayBounds = displayed(rect);
			}
		} else {
			result.limited = true;
		}
		result.entries.push_back(std::move(field));
	}
	return stopped() ? std::nullopt : std::optional{std::move(result)};
}
} // namespace pdf
