import { fireEvent, render, screen } from '@testing-library/react';
import { useState } from 'react';
import { expect, it } from 'vitest';
import PasswordField from './PasswordField';

function Harness() {
  const [value, setValue] = useState('');
  return (
    <PasswordField
      label="Administrator password"
      value={value}
      onChange={(e) => setValue(e.target.value)}
    />
  );
}

it('starts hidden and the eye button shows and hides the password', () => {
  render(<Harness />);
  const field = screen.getByLabelText('Administrator password');
  fireEvent.change(field, { target: { value: 'an-example-password' } });
  expect(field).toHaveAttribute('type', 'password');

  fireEvent.click(screen.getByRole('button', { name: 'Show password' }));
  expect(field).toHaveAttribute('type', 'text');
  expect(field).toHaveValue('an-example-password');
  expect(screen.getByRole('button', { name: 'Hide password' })).toHaveAttribute(
    'aria-pressed',
    'true',
  );

  fireEvent.click(screen.getByRole('button', { name: 'Hide password' }));
  expect(field).toHaveAttribute('type', 'password');
  expect(field).toHaveValue('an-example-password');
});
