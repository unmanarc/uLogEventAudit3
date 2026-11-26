"use strict";

/** @brief Enhance a native select with a searchable dropdown and keyboard selection. */
class AuditSearchableSelect {
    /** @brief Build the dropdown while retaining native values and form validation. */
    constructor(select) {
        this.select = select;
        this.activeIndex = -1;
        const id = select.id;
        this.wrapper = document.createElement("div");
        this.wrapper.className = "searchable-select";
        // IDs come from application markup; option labels are always rendered as text.
        this.wrapper.innerHTML = `
            <button type="button" class="form-select searchable-select-trigger" id="${id}-trigger"
                aria-haspopup="dialog" aria-expanded="false" aria-controls="${id}-popup"></button>
            <div class="searchable-select-popup" id="${id}-popup" role="dialog" aria-label="Choose an option" hidden>
                <input type="search" class="form-control" id="${id}-search" autocomplete="off"
                    role="combobox" aria-autocomplete="list" aria-expanded="false" aria-controls="${id}-list">
                <div class="searchable-select-list" id="${id}-list" role="listbox" aria-label="Available options"></div>
                <div class="form-text searchable-select-status" role="status" aria-live="polite"></div>
            </div>`;
        select.after(this.wrapper);
        this.button = this.wrapper.querySelector("button");
        this.popup = this.wrapper.querySelector(".searchable-select-popup");
        this.search = this.wrapper.querySelector("input");
        this.list = this.wrapper.querySelector("[role=listbox]");
        this.status = this.wrapper.querySelector("[role=status]");
        const label = document.querySelector(`label[for="${id}"]`);
        if (label) {
            label.htmlFor = this.button.id;
        }
        this.search.placeholder = select.dataset.searchable;
        this.search.setAttribute("aria-label", "Search " + (label?.textContent || "options"));
        this.button.setAttribute("aria-describedby", select.getAttribute("aria-describedby") || "");
        select.hidden = true;
        this.button.addEventListener("click", () => {
            if (this.popup.hidden) {
                this.open();
            } else {
                this.close();
            }
        });
        this.button.addEventListener("keydown", event => {
            if (event.key === "ArrowDown" || event.key === "ArrowUp") {
                event.preventDefault();
                this.open();
            }
        });
        this.search.addEventListener("input", () => { this.renderOptions(); });
        this.search.addEventListener("keydown", event => { this.handleKey(event); });
        this.wrapper.addEventListener("focusout", event => {
            if (!this.wrapper.contains(event.relatedTarget)) {
                this.close(false);
            }
        });
        document.addEventListener("pointerdown", event => {
            if (!this.wrapper.contains(event.target)) {
                this.close(false);
            }
        });
        select.addEventListener("change", () => { this.sync(); });
        select.addEventListener("invalid", event => {
            event.preventDefault();
            this.button.setAttribute("aria-invalid", "true");
            this.open();
        });
        // API responses and behavior changes can replace options or disable the select.
        this.observer = new MutationObserver(() => { this.sync(); });
        this.observer.observe(select, {childList: true, subtree: true, characterData: true,
            attributes: true, attributeFilter: ["disabled", "label"]});
        this.sync();
    }

    /** @brief Reflect the saved selection and enabled state in the visible control. */
    sync() {
        this.button.textContent = this.select.selectedOptions[0]?.textContent || "Select an option";
        this.button.disabled = this.select.disabled;
        this.search.disabled = this.select.disabled;
        if (this.select.validity.valid) {
            this.button.removeAttribute("aria-invalid");
        }
        if (this.select.disabled) {
            this.close(false);
        } else if (!this.popup.hidden) {
            this.renderOptions();
        }
    }

    /** @brief Open the dropdown with a fresh query and move focus to its search field. */
    open() {
        if (this.select.disabled) {
            return;
        }
        this.search.value = "";
        this.popup.hidden = false;
        this.button.setAttribute("aria-expanded", "true");
        this.search.setAttribute("aria-expanded", "true");
        this.renderOptions();
        this.search.focus();
    }

    /** @brief Close without changing the selection, optionally restoring focus. */
    close(returnFocus = true) {
        this.popup.hidden = true;
        this.button.setAttribute("aria-expanded", "false");
        this.search.setAttribute("aria-expanded", "false");
        this.search.removeAttribute("aria-activedescendant");
        if (returnFocus) {
            this.button.focus();
        }
    }

    /** @brief Render matching labels safely without mutating the selected value. */
    renderOptions() {
        const query = this.search.value.trim().toLocaleLowerCase();
        this.options = Array.from(this.select.options).filter(option =>
            !option.disabled && !option.hidden && option.textContent.toLocaleLowerCase().includes(query));
        this.list.replaceChildren();
        this.options.forEach((option, index) => {
            const item = document.createElement("div");
            item.id = `${this.select.id}-option-${index}`;
            item.className = "searchable-select-option";
            item.setAttribute("role", "option");
            item.setAttribute("aria-selected", String(option.selected));
            item.textContent = option.textContent;
            // Keep focus in the search field until the click commits the selection.
            item.addEventListener("pointerdown", event => { event.preventDefault(); });
            item.addEventListener("click", () => { this.choose(index); });
            this.list.append(item);
        });
        this.status.textContent = this.options.length ? `${this.options.length} options available.` : "No matching options.";
        this.setActive(this.options.findIndex(option => option.selected));
    }

    /** @brief Announce and scroll the keyboard highlight while retaining input focus. */
    setActive(index) {
        this.activeIndex = index;
        Array.from(this.list.children).forEach((item, itemIndex) => {
            item.classList.toggle("is-active", itemIndex === index);
        });
        const item = this.list.children[index];
        if (item) {
            this.search.setAttribute("aria-activedescendant", item.id);
            item.scrollIntoView({block: "nearest"});
        } else {
            this.search.removeAttribute("aria-activedescendant");
        }
    }

    /** @brief Commit only an explicitly chosen result and notify existing form handlers. */
    choose(index) {
        if (!this.options[index] || this.select.disabled) {
            return;
        }
        this.select.value = this.options[index].value;
        this.select.dispatchEvent(new Event("change", {bubbles: true}));
        this.close();
    }

    /** @brief Handle result navigation, selection, cancellation and normal Tab movement. */
    handleKey(event) {
        if (event.key === "ArrowDown" || event.key === "ArrowUp") {
            event.preventDefault();
            if (this.options.length) {
                const next = this.activeIndex < 0 ? (event.key === "ArrowDown" ? 0 : this.options.length - 1) :
                    (this.activeIndex + (event.key === "ArrowDown" ? 1 : -1) + this.options.length) % this.options.length;
                this.setActive(next);
            }
        } else if (event.key === "Enter") {
            // Enter selects a result; it must never submit the surrounding editor.
            event.preventDefault();
            this.choose(this.activeIndex);
        } else if (event.key === "Escape") {
            event.preventDefault();
            event.stopPropagation();
            this.close();
        } else if (event.key === "Tab") {
            this.close();
        }
    }
}

document.addEventListener("DOMContentLoaded", () => {
    document.querySelectorAll("select[data-searchable]").forEach(select => {
        new AuditSearchableSelect(select);
    });
});
