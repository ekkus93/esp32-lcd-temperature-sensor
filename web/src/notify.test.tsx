import {
  fireEvent,
  render as plainRender,
  screen,
} from '@testing-library/react';
import { expect, it } from 'vitest';
import { NoticeProvider } from './notify';
import { useNotify } from './noticeContext';
import { renderThemed as render } from './renderThemed';

function Saver({ text }: { text: string }) {
  const notify = useNotify();
  return <button onClick={() => notify(text)}>save {text}</button>;
}

it('shows a completed save as a notice, and a later one replaces it', async () => {
  render(
    <NoticeProvider>
      <Saver text="Device preferences saved." />
      <Saver text="Settings committed and applied." />
    </NoticeProvider>,
  );
  fireEvent.click(
    screen.getByRole('button', { name: 'save Device preferences saved.' }),
  );
  expect(
    await screen.findByText('Device preferences saved.'),
  ).toBeInTheDocument();
  fireEvent.click(
    screen.getByRole('button', {
      name: 'save Settings committed and applied.',
    }),
  );
  expect(
    await screen.findByText('Settings committed and applied.'),
  ).toBeInTheDocument();
  expect(screen.queryByText('Device preferences saved.')).toBeNull();
});

it('does nothing without a provider, so views still render in isolation', () => {
  plainRender(<Saver text="unseen" />);
  fireEvent.click(screen.getByRole('button', { name: 'save unseen' }));
  expect(screen.queryByText('unseen')).toBeNull();
});
