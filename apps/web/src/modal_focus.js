// Keep keyboard navigation inside the active dialog and restore the trigger.
export const modalFocus = {
  mounted(element, binding) {
    const previous = document.activeElement;
    const controls = () =>
      [...element.querySelectorAll('button, input, select, a[href]')].filter(
        (control) => !control.disabled && control.getClientRects().length,
      );
    const onKey = (event) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        binding.value();
      }
      if (event.key !== 'Tab') return;
      const focusable = controls();
      const first = focusable[0];
      const last = focusable.at(-1);
      if (
        event.shiftKey &&
        (document.activeElement === first || !element.contains(document.activeElement))
      ) {
        event.preventDefault();
        last?.focus();
      } else if (
        !event.shiftKey &&
        (document.activeElement === last || !element.contains(document.activeElement))
      ) {
        event.preventDefault();
        first?.focus();
      }
    };
    document.addEventListener('keydown', onKey);
    element.releaseFocus = () => {
      document.removeEventListener('keydown', onKey);
      if (previous?.isConnected) previous.focus();
    };
    queueMicrotask(() =>
      (element.querySelector('input:not([readonly])') || controls()[0])?.focus(),
    );
  },
  unmounted(element) {
    element.releaseFocus?.();
  },
};
