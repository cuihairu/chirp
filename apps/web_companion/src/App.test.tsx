import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';
import App from './App';
import { createServices } from './api/services';
import { zh } from './i18n/zh';
import { FakeChatConnection } from './state/test_helpers';

describe('App routing', () => {
  it('lands logged-out users on the login page', () => {
    render(<App services={createServices({ conn: new FakeChatConnection() })} />);
    expect(screen.getByText(zh.login.title)).toBeTruthy();
  });
});
