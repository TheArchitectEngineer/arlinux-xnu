"""Runner-only normal browser download for declared JavaScript download sites.
No credentials, CAPTCHA solving, stealth options or copied browser sessions.
The caller still verifies the exact archive size and SHA-256.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
from urllib.parse import urlsplit


def main():
    parser=argparse.ArgumentParser()
    for name in ('url','page','output','evidence'):parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    if os.environ.get('GITHUB_ACTIONS')!='true':raise RuntimeError('Browser input download requires the hosted build entry')
    if urlsplit(args.page).scheme!='https' or urlsplit(args.url).netloc!=urlsplit(args.page).netloc:
        raise RuntimeError('Browser download page must share the declared HTTPS source origin')
    root=Path(__file__).resolve().parents[2]
    packages=root/'build/browser-download-python'
    subprocess.run([sys.executable,'-m','pip','install','--disable-pip-version-check',
        '--only-binary=:all:','--require-hashes','--target',str(packages),
        '-r',str(root/'tools/browser-download-requirements.txt')],check=True,timeout=60)
    sys.path.insert(0,str(packages))
    from playwright.sync_api import sync_playwright,Error
    evidence=Path(args.evidence);evidence.mkdir(parents=True,exist_ok=True)
    with sync_playwright() as automation:
        # Use the runner's normal installed browser, with an empty temporary
        # profile. If the site requires a human/login, retain the page and fail.
        browser=automation.chromium.launch(channel='chrome',headless=False,timeout=15000)
        context=browser.new_context(accept_downloads=True)
        page=context.new_page();page.set_default_timeout(15000)
        try:
            page.goto(args.page,wait_until='domcontentloaded')
            page.get_by_role('button',name='Search',exact=True).wait_for(state='visible')
            with page.expect_download(timeout=30000) as pending:
                try:page.goto(args.url,wait_until='commit')
                except Error as failure:
                    if 'ERR_ABORTED' not in str(failure):raise
            pending.value.save_as(args.output)
        finally:
            try:
                page.screenshot(path=str(evidence/'download-page.png'))
                (evidence/'download-page.txt').write_text(page.locator('body').inner_text(),encoding='utf-8')
            finally:
                context.close();browser.close()


if __name__=='__main__':main()
