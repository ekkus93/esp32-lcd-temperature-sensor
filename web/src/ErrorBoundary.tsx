import { Component, type ErrorInfo, type ReactNode } from 'react';

// Shows a message with a Reload button instead of a blank page when something fails while drawing
// the page, for example code that could not be loaded after a firmware update.
export default class ErrorBoundary extends Component<
  { children: ReactNode },
  { failed: boolean }
> {
  state = { failed: false };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  componentDidCatch(error: unknown, info: ErrorInfo) {
    console.error('Page failed to render', error, info.componentStack);
  }

  render() {
    if (!this.state.failed) return this.props.children;
    return (
      <main role="alert" style={{ padding: 24, maxWidth: 560 }}>
        <h1 style={{ fontSize: 24 }}>This page could not be shown</h1>
        <p>
          If the monitor&apos;s firmware was just updated, this browser still
          has the old page. Reload to get the new one.
        </p>
        <button type="button" onClick={() => location.reload()}>
          Reload
        </button>
      </main>
    );
  }
}
