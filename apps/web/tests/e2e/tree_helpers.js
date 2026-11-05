import { expect } from '@playwright/test';
export async function expandPlatforms(page) {
  await expect(page.locator('.platform-tree-row').first()).toBeVisible();
  for (const button of await page.getByRole('button', { name: /^展开平台 / }).all()) {
    await button.click();
  }
}
