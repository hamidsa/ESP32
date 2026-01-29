from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import mysql.connector
import json
import os
from datetime import datetime
import socket
import logging
import sys
import hashlib
import secrets
import random
import time
import base64
from collections import defaultdict

# تنظیمات لاگ ساده (فقط کنسول)
logging.basicConfig(
    level=logging.WARNING,  # فقط خطاهای مهم
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[logging.StreamHandler(sys.stdout)]
)

logger = logging.getLogger(__name__)

app = Flask(__name__)

# تنظیمات CORS
CORS(app, resources={
    r"/*": {
        "origins": ["*"],
        "methods": ["GET", "POST", "PUT", "DELETE", "OPTIONS"],
        "allow_headers": ["Content-Type", "Authorization"]
    }
})

# Middleware برای CORS
@app.after_request
def after_request(response):
    response.headers.add('Access-Control-Allow-Origin', '*')
    response.headers.add('Access-Control-Allow-Headers', 'Content-Type,Authorization')
    response.headers.add('Access-Control-Allow-Methods', 'GET,PUT,POST,DELETE,OPTIONS')
    return response

# تنظیمات MySQL
DB_CONFIG = {
    'host': 'localhost',
    'user': 'root',           
    'password': 'HSR@d2000',      
    'database': 'crypto_monitoring'
}

# ذخیره کپچاهای فعال
active_captchas = {}
registration_attempts = defaultdict(list)

def get_db_connection():
    """ایجاد اتصال به MySQL"""
    try:
        conn = mysql.connector.connect(**DB_CONFIG)
        return conn
    except Exception as e:
        logger.error(f"MySQL Connection Error: {e}")
        return None

def init_database():
    """راه‌اندازی اولیه دیتابیس"""
    conn = None
    cursor = None
    try:
        conn = get_db_connection()
        if not conn:
            return False
        
        cursor = conn.cursor()
        
        # ایجاد جدول کاربران
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS users (
                id INT AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(50) UNIQUE NOT NULL,
                password_hash VARCHAR(255) NOT NULL,
                email VARCHAR(100),
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                is_active BOOLEAN DEFAULT TRUE
            )
        ''')
        
        # ایجاد جدول پورتفوها
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS user_portfolios (
                id INT AUTO_INCREMENT PRIMARY KEY,
                user_id INT NOT NULL,
                portfolio_name VARCHAR(100) DEFAULT 'Main',
                portfolio_data JSON NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                FOREIGN KEY (user_id) REFERENCES users(id),
                UNIQUE KEY unique_user_portfolio (user_id, portfolio_name)
            )
        ''')
        
        # ایجاد جدول futures_prices_4h برای پردازش داده‌های 4 ساعت اخیر
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS futures_prices_4h (
                id INT AUTO_INCREMENT PRIMARY KEY,
                symbol VARCHAR(50) NOT NULL,
                price DECIMAL(20, 8),
                timestamp DATETIME,
                previous_price DECIMAL(20, 8),
                price_diff DECIMAL(20, 8),
                price_diff_pct DECIMAL(10, 4),
                INDEX idx_symbol (symbol),
                INDEX idx_timestamp (timestamp)
            )
        ''')
        
        # ایجاد جدول kcex_futures (اگر وجود ندارد)
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS kcex_futures (
                id INT AUTO_INCREMENT PRIMARY KEY,
                symbol VARCHAR(50) NOT NULL UNIQUE,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_symbol (symbol)
            )
        ''')
        
        # وارد کردن برخی داده‌های نمونه به kcex_futures
        try:
            kcex_symbols = ['BTC_USDT', 'ETH_USDT', 'ADA_USDT', 'LINK_USDT', 
                           'LTC_USDT', 'BCH_USDT', 'XRP_USDT', 'DOT_USDT']

            for symbol in kcex_symbols:
                cursor.execute('''
                    INSERT IGNORE INTO kcex_futures (symbol) VALUES (%s)
                ''', (symbol,))
            
            logger.info(f"Inserted {len(kcex_symbols)} sample symbols into kcex_futures")
        except Exception as e:
            logger.warning(f"Could not insert sample kcex data: {e}")
        
        conn.commit()
        logger.info("Database initialized successfully")
        return True
        
    except Exception as e:
        logger.error(f"Error initializing database: {e}")
        return False
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

def hash_password(password):
    """هش کردن رمز عبور"""
    return hashlib.sha256(password.encode()).hexdigest()

def generate_captcha():
    """تولید سوال کپچا"""
    try:
        operations = [
            {'type': 'add', 'text': '+', 'func': lambda a, b: a + b},
            {'type': 'subtract', 'text': '-', 'func': lambda a, b: a - b},
            {'type': 'multiply', 'text': '×', 'func': lambda a, b: a * b}
        ]
        
        operation = random.choice(operations)
        
        if operation['type'] == 'subtract':
            # برای تفریق: عدد اول همیشه بزرگتر یا مساوی عدد دوم باشد
            num1 = random.randint(5, 15)
            num2 = random.randint(1, num1)
        elif operation['type'] == 'multiply':
            # برای ضرب: اعداد کوچکتر برای سادگی
            num1 = random.randint(2, 8)
            num2 = random.randint(2, 8)
        else:
            # برای جمع: اعداد معمولی
            num1 = random.randint(1, 10)
            num2 = random.randint(1, 10)
        
        captcha_id = str(random.randint(100000, 999999))
        answer = operation['func'](num1, num2)
        
        # برای نمایش درست در محیط فارسی: عدد بزرگتر در سمت راست
        # در HTML فارسی، ترتیب از راست به چپ است، پس باید اعداد را معکوس کنیم
        display_num1 = num2  # نمایش در سمت چپ
        display_num2 = num1  # نمایش در سمت راست
        
        active_captchas[captcha_id] = {
            'question': f"{display_num1} {operation['text']} {display_num2}",
            'answer': str(answer),
            'created_at': time.time()
        }
        
        # پاک کردن کپچاهای قدیمی (بیش از 10 دقیقه)
        cleanup_old_captchas()
        
        logger.info(f"Generated captcha: {captcha_id} - {display_num1} {operation['text']} {display_num2} = {answer}")
        logger.info(f"Actual calculation: {num1} {operation['text']} {num2} = {answer}")
        return captcha_id, active_captchas[captcha_id]['question']
        
    except Exception as e:
        logger.error(f"Error generating captcha: {e}")
        return None, None

def cleanup_old_captchas():
    """پاک کردن کپچاهای قدیمی"""
    current_time = time.time()
    expired_ids = []
    
    for captcha_id, captcha_data in active_captchas.items():
        if current_time - captcha_data['created_at'] > 600:  # 10 دقیقه
            expired_ids.append(captcha_id)
    
    for captcha_id in expired_ids:
        del active_captchas[captcha_id]

def verify_captcha(captcha_id, user_answer):
    """تایید پاسخ کپچا"""
    try:
        logger.info(f"Verifying captcha: {captcha_id}, user answer: {user_answer}")
        
        if not captcha_id or captcha_id not in active_captchas:
            logger.error(f"Captcha ID not found: {captcha_id}")
            return False
        
        captcha_data = active_captchas[captcha_id]
        expected_answer = captcha_data['answer']
        user_answer_clean = str(user_answer).strip()
        
        logger.info(f"Expected: {expected_answer}, Got: {user_answer_clean}")
        
        # پاک کردن کپچا بعد از استفاده
        del active_captchas[captcha_id]
        
        # بررسی زمان انقضا
        if time.time() - captcha_data['created_at'] > 600:  # 10 دقیقه
            logger.error("Captcha expired")
            return False
        
        result = expected_answer == user_answer_clean
        logger.info(f"Captcha verification result: {result}")
        return result
        
    except Exception as e:
        logger.error(f"Error verifying captcha: {e}")
        return False

def is_registration_allowed(ip_address, username):
    """بررسی مجاز بودن ثبت نام"""
    current_time = time.time()
    
    # پاک کردن تلاش‌های قدیمی (بیش از 1 ساعت)
    registration_attempts[ip_address] = [
        attempt for attempt in registration_attempts[ip_address]
        if current_time - attempt['time'] < 3600
    ]
    
    # محدودیت: حداکثر 5 تلاش در ساعت از یک IP
    if len(registration_attempts[ip_address]) >= 5:
        return False, "تعداد تلاش‌های ثبت نام از این آدرس IP بیش از حد مجاز است. لطفاً یک ساعت دیگر تلاش کنید."
    
    return True, ""

# API Routes
@app.route('/api/captcha/generate', methods=['GET'])
def generate_captcha_api():
    """API برای تولید کپچا جدید"""
    try:
        captcha_id, question = generate_captcha()
        if captcha_id and question:
            return jsonify({
                'success': True,
                'captcha_id': captcha_id,
                'question': question
            })
        else:
            return jsonify({'success': False, 'error': 'خطا در تولید کپچا'})
    except Exception as e:
        logger.error(f"Error generating captcha: {e}")
        return jsonify({'success': False, 'error': 'خطا در تولید کپچا'})

@app.route('/api/register', methods=['POST'])
def register_user():
    """ثبت نام کاربر جدید"""
    conn = None
    cursor = None
    try:
        data = request.json
        username = data.get('username')
        password = data.get('password')
        email = data.get('email')
        captcha_id = data.get('captcha_id')
        captcha_answer = data.get('captcha_answer')
        client_ip = request.remote_addr
        
        logger.info(f"Registration attempt - IP: {client_ip}, Username: {username}")
        
        # بررسی محدودیت‌های ثبت نام
        is_allowed, error_msg = is_registration_allowed(client_ip, username)
        if not is_allowed:
            logger.warning(f"Registration blocked for IP {client_ip}: {error_msg}")
            return jsonify({'success': False, 'error': error_msg})
        
        # تایید کپچا
        if not captcha_id or not captcha_answer:
            registration_attempts[client_ip].append({
                'time': time.time(),
                'username': username,
                'success': False,
                'reason': 'Missing captcha'
            })
            return jsonify({'success': False, 'error': 'لطفاً کپچا را تکمیل کنید'})
        
        if not verify_captcha(captcha_id, captcha_answer):
            registration_attempts[client_ip].append({
                'time': time.time(),
                'username': username,
                'success': False,
                'reason': 'Wrong captcha'
            })
            return jsonify({'success': False, 'error': 'پاسخ کپچا نادرست است'})
        
        if not username or not password:
            return jsonify({'success': False, 'error': 'نام کاربری و رمز عبور الزامی است'})
        
        if len(username) < 3:
            return jsonify({'success': False, 'error': 'نام کاربری باید حداقل ۳ کاراکتر باشد'})
        
        if len(password) < 6:
            return jsonify({'success': False, 'error': 'رمز عبور باید حداقل ۶ کاراکتر باشد'})
        
        # بررسی کاراکترهای مجاز در نام کاربری
        if not all(c.isalnum() or c == '_' for c in username):
            return jsonify({'success': False, 'error': 'نام کاربری فقط می‌تواند شامل حروف انگلیسی، اعداد و underline باشد'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'خطا در اتصال به پایگاه داده'})
        
        cursor = conn.cursor()
        
        # بررسی وجود کاربر
        cursor.execute("SELECT id FROM users WHERE username = %s", (username,))
        if cursor.fetchone():
            registration_attempts[client_ip].append({
                'time': time.time(),
                'username': username,
                'success': False,
                'reason': 'Username exists'
            })
            return jsonify({'success': False, 'error': 'نام کاربری قبلاً استفاده شده است'})
        
        # ایجاد کاربر جدید
        password_hash = hash_password(password)
        cursor.execute(
            "INSERT INTO users (username, password_hash, email) VALUES (%s, %s, %s)",
            (username, password_hash, email)
        )
        
        conn.commit()
        
        # ثبت تلاش موفق
        registration_attempts[client_ip].append({
            'time': time.time(),
            'username': username,
            'success': True
        })
        
        logger.info(f"New user registered successfully: {username} from IP: {client_ip}")
        return jsonify({'success': True, 'message': 'کاربر با موفقیت ثبت نام شد'})
        
    except mysql.connector.Error as e:
        logger.error(f"MySQL Error in register_user: {e}")
        return jsonify({'success': False, 'error': 'خطای پایگاه داده'})
    except Exception as e:
        logger.error(f"Error in register_user: {e}")
        return jsonify({'success': False, 'error': 'خطای سرور'})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/login', methods=['POST'])
def login_user():
    """ورود کاربر"""
    conn = None
    cursor = None
    try:
        data = request.json
        username = data.get('username')
        password = data.get('password')
        
        if not username or not password:
            return jsonify({'success': False, 'error': 'نام کاربری و رمز عبور الزامی است'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor(dictionary=True)
        
        # بررسی کاربر
        cursor.execute("SELECT id, username, password_hash FROM users WHERE username = %s AND is_active = TRUE", (username,))
        user = cursor.fetchone()
        
        if not user:
            return jsonify({'success': False, 'error': 'نام کاربری یا رمز عبور اشتباه است'})
        
        if user['password_hash'] != hash_password(password):
            return jsonify({'success': False, 'error': 'نام کاربری یا رمز عبور اشتباه است'})
        
        logger.info(f"User logged in: {username}")
        return jsonify({
            'success': True, 
            'message': 'ورود موفق',
            'user': {
                'id': user['id'],
                'username': user['username']
            }
        })
        
    except Exception as e:
        logger.error(f"Error in login_user: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/symbols', methods=['GET'])
def get_all_symbols():
    """دریافت تمام symbolهای موجود با وضعیت KCEX"""
    conn = None
    cursor = None
    try:
        logger.info("API /api/symbols called")
        
        conn = get_db_connection()
        if not conn:
            logger.error("Database connection failed")
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor()
        
        # دریافت تمام symbolها بدون محدودیت
        cursor.execute("SELECT DISTINCT symbol FROM futures_prices WHERE symbol LIKE '%_USDT' ORDER BY symbol")
        symbols = [row[0] for row in cursor.fetchall()]
        
        symbols_with_kcex = []
        
        # بررسی وضعیت KCEX برای هر symbol
        for symbol in symbols:
            cursor.execute("SELECT COUNT(*) as count FROM kcex_futures WHERE symbol = %s", (symbol,))
            result = cursor.fetchone()
            in_kcex = result[0] > 0 if result else False
            
            symbols_with_kcex.append({
                'symbol': symbol,
                'in_kcex': in_kcex
            })
        
        if symbols_with_kcex:
            logger.info(f"Found {len(symbols_with_kcex)} symbols with KCEX status")
            return jsonify({
                'success': True,
                'symbols': symbols_with_kcex,
                'count': len(symbols_with_kcex),
                'source': 'futures_prices'
            })
        else:
            # استفاده از لیست پیش‌فرض کامل‌تر
            default_symbols = [
                {'symbol': 'BTC_USDT', 'in_kcex': True},
                {'symbol': 'ETH_USDT', 'in_kcex': True},
                {'symbol': 'BNB_USDT', 'in_kcex': False},
                {'symbol': 'ADA_USDT', 'in_kcex': True},
                {'symbol': 'DOT_USDT', 'in_kcex': False},
                {'symbol': 'LINK_USDT', 'in_kcex': True},
                {'symbol': 'LTC_USDT', 'in_kcex': True},
                {'symbol': 'BCH_USDT', 'in_kcex': True},
                {'symbol': 'XLM_USDT', 'in_kcex': False},
                {'symbol': 'XRP_USDT', 'in_kcex': True}
            ]
            logger.info(f"Using default symbols: {len(default_symbols)} symbols")
            return jsonify({
                'success': True,
                'symbols': default_symbols,
                'count': len(default_symbols),
                'source': 'default'
            })
        
    except Exception as e:
        logger.error(f"Exception in get_all_symbols: {e}")
        default_symbols = [
            {'symbol': 'BTC_USDT', 'in_kcex': True},
            {'symbol': 'ETH_USDT', 'in_kcex': True},
            {'symbol': 'ADA_USDT', 'in_kcex': True}
        ]
        return jsonify({
            'success': True,
            'symbols': default_symbols,
            'count': len(default_symbols),
            'source': 'error_fallback'
        })
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/portfolio/<int:user_id>', methods=['GET'])
def get_portfolio(user_id):
    """دریافت پورتفوی کاربر"""
    conn = None
    cursor = None
    try:
        portfolio_name = request.args.get('portfolio_name', 'Main')
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor(dictionary=True)
        
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = %s
        """, (user_id, portfolio_name))
        
        result = cursor.fetchone()
        
        if result:
            portfolio_data = result['portfolio_data']
            if isinstance(portfolio_data, str):
                portfolio_data = json.loads(portfolio_data)
            
            # تضمین وجود quantity برای همه آیتم‌ها
            for item in portfolio_data:
                if 'quantity' not in item or item['quantity'] <= 0:
                    item['quantity'] = 1.0
            
            return jsonify({
                'success': True,
                'portfolio': portfolio_data,
                'portfolio_name': portfolio_name
            })
        else:
            return jsonify({
                'success': True,
                'portfolio': [],
                'portfolio_name': portfolio_name
            })
            
    except Exception as e:
        logger.error(f"Error in get_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/portfolio/<int:user_id>', methods=['POST'])
def update_portfolio(user_id):
    """بروزرسانی پورتفوی کاربر"""
    conn = None
    cursor = None
    try:
        data = request.json
        portfolio_data = data.get('portfolio', [])
        portfolio_name = data.get('portfolio_name', 'Main')
        
        # اعتبارسنجی داده‌ها
        for item in portfolio_data:
            if 'symbol' not in item or 'position' not in item:
                return jsonify({'success': False, 'error': 'Invalid portfolio data'})
            if 'quantity' not in item or item['quantity'] <= 0:
                item['quantity'] = 1.0  # مقدار پیش‌فرض
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO user_portfolios (user_id, portfolio_name, portfolio_data) 
            VALUES (%s, %s, %s) 
            ON DUPLICATE KEY UPDATE portfolio_data = %s, updated_at = %s
        ''', (user_id, portfolio_name, json.dumps(portfolio_data), json.dumps(portfolio_data), datetime.now()))
        
        conn.commit()
        logger.info(f"Portfolio updated for user {user_id} with {len(portfolio_data)} items")
        return jsonify({'success': True, 'message': 'Portfolio updated successfully'})
        
    except Exception as e:
        logger.error(f"Error in update_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/portfolio/analyze/<int:user_id>', methods=['GET'])
def analyze_portfolio(user_id):
    """تحلیل پورتفو - نسخه ساده و قابل اعتماد"""
    conn = None
    cursor = None
    try:
        portfolio_name = request.args.get('portfolio_name', 'Main')
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor(dictionary=True)
        
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = %s
        """, (user_id, portfolio_name))
        
        result = cursor.fetchone()
        
        if not result:
            return jsonify({'success': False, 'error': 'Portfolio not found'})
        
        portfolio_data = result['portfolio_data']
        if isinstance(portfolio_data, str):
            portfolio_data = json.loads(portfolio_data)
        
        if not isinstance(portfolio_data, list) or len(portfolio_data) == 0:
            return jsonify({'success': False, 'error': 'Portfolio is empty'})
        
        analyzed_portfolio = []
        total_investment = 0
        total_current_value = 0
        long_pnl_total = 0
        short_pnl_total = 0
        long_count = 0
        short_count = 0
        
        for item in portfolio_data:
            if not item or 'symbol' not in item or 'position' not in item:
                continue
                
            symbol = item['symbol']
            position = item['position']
            entry_price = float(item.get('entry_price', 0))
            quantity = float(item.get('quantity', 1.0))
            
            if entry_price <= 0 or quantity <= 0:
                continue
            
            # دریافت آخرین قیمت
            cursor.execute("""
                SELECT price FROM futures_prices 
                WHERE symbol = %s 
                ORDER BY timestamp DESC 
                LIMIT 1
            """, (symbol,))
            price_result = cursor.fetchone()
            
            if not price_result or price_result['price'] is None:
                continue
                
            current_price = float(price_result['price'])
            
            # محاسبات پایه
            investment = entry_price * quantity
            current_value = current_price * quantity
            
            # محاسبه PNL
            if position == 'long':
                pnl = (current_price - entry_price) * quantity
                long_pnl_total += pnl
                long_count += 1
            else:  # short
                pnl = (entry_price - current_price) * quantity
                short_pnl_total += pnl
                short_count += 1
            
            total_investment += investment
            total_current_value += current_value
            
            # بررسی وضعیت KCEX
            cursor.execute("SELECT COUNT(*) as count FROM kcex_futures WHERE symbol = %s", (symbol,))
            kcex_result = cursor.fetchone()
            in_kcex = kcex_result['count'] > 0 if kcex_result else False
            
            analyzed_item = {
                'symbol': symbol,
                'position': position,
                'entry_price': round(entry_price, 8),
                'current_price': round(current_price, 8),
                'quantity': round(quantity, 8),
                'investment': round(investment, 2),
                'current_value': round(current_value, 2),
                'pnl': round(pnl, 2),
                'pnl_percent': round((pnl / investment * 100) if investment > 0 else 0, 2),
                'is_profitable': pnl >= 0,
                'in_kcex': in_kcex
            }
            analyzed_portfolio.append(analyzed_item)
        
        # محاسبات نهایی
        total_pnl = long_pnl_total + short_pnl_total
        
        # اعتبارسنجی: بررسی تناقض
        calculated_pnl_from_diff = total_current_value - total_investment
        discrepancy = abs(total_pnl - calculated_pnl_from_diff)
        
        if discrepancy > 0.01:
            logger.warning(f"PNL discrepancy detected: {discrepancy}")
            logger.warning(f"Using difference method for total PNL")
            total_pnl = calculated_pnl_from_diff
        
        # محاسبه درصدها
        if total_investment > 0:
            total_pnl_percent = (total_pnl / total_investment * 100)
            long_pnl_percent = (long_pnl_total / total_investment * 100) if total_investment > 0 else 0
            short_pnl_percent = (short_pnl_total / total_investment * 100) if total_investment > 0 else 0
        else:
            total_pnl_percent = 0
            long_pnl_percent = 0
            short_pnl_percent = 0
        
        return jsonify({
            'success': True,
            'portfolio': analyzed_portfolio,
            'summary': {
                'total_investment': round(total_investment, 2),
                'total_current_value': round(total_current_value, 2),
                'total_pnl': round(total_pnl, 2),
                'total_pnl_percent': round(total_pnl_percent, 2),
                'long_positions': long_count,
                'short_positions': short_count,
                'long_total_pnl': round(long_pnl_total, 2),
                'short_total_pnl': round(short_pnl_total, 2),
                'long_pnl_percent': round(long_pnl_percent, 2),
                'short_pnl_percent': round(short_pnl_percent, 2),
                'timestamp': datetime.now().isoformat(),
                'verification': {
                    'long_short_sum': round(long_pnl_total + short_pnl_total, 2),
                    'difference_method': round(calculated_pnl_from_diff, 2),
                    'discrepancy': round(discrepancy, 4)
                }
            }
        })
        
    except Exception as e:
        logger.error(f"Exception in analyze_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()
                        
@app.route('/api/portfolio/accurate_analysis/<int:user_id>', methods=['GET'])
def accurate_portfolio_analysis(user_id):
    """تحلیل دقیق پورتفو با لاگ‌گذاری کامل"""
    conn = None
    cursor = None
    try:
        portfolio_name = request.args.get('portfolio_name', 'Main')
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor(dictionary=True)
        
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = %s
        """, (user_id, portfolio_name))
        
        result = cursor.fetchone()
        
        if not result:
            return jsonify({'success': False, 'error': 'Portfolio not found'})
        
        portfolio_data = result['portfolio_data']
        if isinstance(portfolio_data, str):
            portfolio_data = json.loads(portfolio_data)
        
        if not isinstance(portfolio_data, list) or len(portfolio_data) == 0:
            return jsonify({'success': False, 'error': 'Portfolio is empty'})
        
        analyzed_portfolio = []
        total_investment = 0
        total_current_value = 0
        total_pnl_from_items = 0
        valid_items = 0
        
        for item in portfolio_data:
            if not item or 'symbol' not in item or 'position' not in item:
                continue
                
            symbol = item['symbol']
            position = item['position']
            entry_price = float(item.get('entry_price', 0))
            quantity = float(item.get('quantity', 1.0))
            
            if entry_price <= 0 or quantity <= 0:
                continue
            
            # دریافت آخرین قیمت
            cursor.execute("""
                SELECT price FROM futures_prices 
                WHERE symbol = %s 
                ORDER BY timestamp DESC 
                LIMIT 1
            """, (symbol,))
            price_result = cursor.fetchone()
            
            if not price_result or price_result['price'] is None:
                continue
                
            current_price = float(price_result['price'])
            
            # محاسبات پایه
            investment = entry_price * quantity
            current_value = current_price * quantity
            
            # محاسبه PNL
            if position == 'long':
                pnl = (current_price - entry_price) * quantity
            else:  # short
                pnl = (entry_price - current_price) * quantity
            
            total_investment += investment
            total_current_value += current_value
            total_pnl_from_items += pnl
            valid_items += 1
            
            # بررسی وضعیت KCEX
            cursor.execute("SELECT COUNT(*) as count FROM kcex_futures WHERE symbol = %s", (symbol,))
            kcex_result = cursor.fetchone()
            in_kcex = kcex_result['count'] > 0 if kcex_result else False
            
            analyzed_item = {
                'symbol': symbol,
                'position': position,
                'entry_price': round(entry_price, 8),
                'current_price': round(current_price, 8),
                'quantity': round(quantity, 8),
                'investment': round(investment, 2),
                'current_value': round(current_value, 2),
                'pnl': round(pnl, 2),
                'pnl_percent': round((pnl / investment * 100) if investment > 0 else 0, 2),
                'is_profitable': pnl >= 0,
                'in_kcex': in_kcex,
                'calculation_details': {
                    'entry_price': round(entry_price, 8),
                    'current_price': round(current_price, 8),
                    'quantity': round(quantity, 8),
                    'position': position,
                    'pnl_formula': 'long: (current - entry) × quantity, short: (entry - current) × quantity',
                    'verified': True
                }
            }
            analyzed_portfolio.append(analyzed_item)
        
        # محاسبات نهایی
        if total_investment > 0:
            total_pnl_percent_from_items = (total_pnl_from_items / total_investment * 100)
        else:
            total_pnl_percent_from_items = 0
        
        # محاسبه تعداد موقعیت‌ها
        long_count = sum(1 for item in analyzed_portfolio if item['position'] == 'long')
        short_count = sum(1 for item in analyzed_portfolio if item['position'] == 'short')
        long_total_pnl = sum(item['pnl'] for item in analyzed_portfolio if item['position'] == 'long')
        short_total_pnl = sum(item['pnl'] for item in analyzed_portfolio if item['position'] == 'short')
        
        summary = {
            'total_investment': round(total_investment, 2),
            'total_current_value': round(total_current_value, 2),
            'total_pnl': round(total_pnl_from_items, 2),
            'total_pnl_alt': round(total_current_value - total_investment, 2),
            'total_pnl_percent': round(total_pnl_percent_from_items, 2),
            'avg_pnl_per_position': round(total_pnl_from_items / valid_items, 2) if valid_items > 0 else 0,
            'long_positions': long_count,
            'short_positions': short_count,
            'long_total_pnl': round(long_total_pnl, 2),
            'short_total_pnl': round(short_total_pnl, 2),
            'verification': {
                'items_sum': round(total_pnl_from_items, 2),
                'difference_method': round(total_current_value - total_investment, 2),
                'discrepancy': round(abs(total_pnl_from_items - (total_current_value - total_investment)), 4),
                'status': 'OK' if abs(total_pnl_from_items - (total_current_value - total_investment)) < 0.01 else 'WARNING'
            }
        }
        
        return jsonify({
            'success': True,
            'portfolio': analyzed_portfolio,
            'summary': summary,
            'metadata': {
                'calculation_time': datetime.now().isoformat(),
                'data_source': 'futures_prices',
                'version': '2.0'
            }
        })
        
    except Exception as e:
        logger.error(f"Exception in accurate_portfolio_analysis: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()
    	
@app.route('/api/debug/short_positions/<int:user_id>', methods=['GET'])
def debug_short_positions(user_id):
    """API دیباگ برای بررسی محاسبات شورت"""
    portfolio_name = request.args.get('portfolio_name', 'Arduino')
    
    conn = get_db_connection()
    cursor = conn.cursor(dictionary=True)
    
    cursor.execute("""
        SELECT portfolio_data FROM user_portfolios 
        WHERE user_id = %s AND portfolio_name = %s
    """, (user_id, portfolio_name))
    
    result = cursor.fetchone()
    portfolio_data = json.loads(result['portfolio_data']) if result else []
    
    short_positions = []
    total_pnl = 0
    
    for item in portfolio_data:
        if item.get('position') == 'short':
            symbol = item['symbol']
            entry_price = float(item.get('entry_price', 0))
            quantity = float(item.get('quantity', 1.0))
            
            # دریافت قیمت جاری
            cursor.execute("SELECT price FROM futures_prices WHERE symbol = %s ORDER BY timestamp DESC LIMIT 1", (symbol,))
            price_result = cursor.fetchone()
            current_price = float(price_result['price']) if price_result else 0
            
            # محاسبه PNL
            pnl = (entry_price - current_price) * quantity
            
            short_positions.append({
                'symbol': symbol,
                'entry_price': entry_price,
                'current_price': current_price,
                'quantity': quantity,
                'pnl_calculated': pnl,
                'pnl_stored': item.get('pnl', 0) if 'pnl' in item else 'N/A'
            })
            
            total_pnl += pnl
    
    cursor.close()
    conn.close()
    
    return jsonify({
        'success': True,
        'total_short_positions': len(short_positions),
        'total_pnl_calculated': total_pnl,
        'positions': short_positions,
        'debug_info': {
            'formula': 'PNL = (Entry - Current) × Quantity',
            'note': 'برای موقعیت‌های شورت'
        }
    })
                                                  
@app.route('/api/portfolio/<int:user_id>', methods=['DELETE'])
def delete_portfolio(user_id):
    """حذف کامل پورتفو"""
    conn = None
    cursor = None
    try:
        portfolio_name = request.args.get('portfolio_name')
        
        if not portfolio_name:
            return jsonify({'success': False, 'error': 'نام پورتفو الزامی است'})
        
        if portfolio_name == 'Main':
            return jsonify({'success': False, 'error': 'پورتفوی اصلی قابل حذف نیست'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor()
        
        # حذف پورتفو از دیتابیس
        cursor.execute(
            "DELETE FROM user_portfolios WHERE user_id = %s AND portfolio_name = %s",
            (user_id, portfolio_name)
        )
        
        if cursor.rowcount > 0:
            conn.commit()
            logger.info(f"Portfolio '{portfolio_name}' deleted for user {user_id}")
            return jsonify({'success': True, 'message': 'پورتفو با موفقیت حذف شد'})
        else:
            return jsonify({'success': False, 'error': 'پورتفو یافت نشد'})
            
    except Exception as e:
        logger.error(f"Error in delete_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/portfolio/<int:user_id>/add_to_main', methods=['POST'])
def add_to_main_portfolio(user_id):
    """افزودن سریع موقعیت به پورتفوی Main"""
    conn = None
    cursor = None
    try:
        data = request.json
        symbol = data.get('symbol')
        position = data.get('position')
        entry_price = float(data.get('entry_price', 0))
        quantity = float(data.get('quantity', 1.0))
        
        if not symbol or not position or entry_price <= 0:
            return jsonify({'success': False, 'error': 'داده‌های ورودی نامعتبر'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'خطا در اتصال به پایگاه داده'})
        
        cursor = conn.cursor()
        
        # دریافت پورتفوی Main فعلی
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = 'Main'
        """, (user_id,))
        
        result = cursor.fetchone()
        
        if result:
            portfolio_data = result[0]
            if isinstance(portfolio_data, str):
                portfolio_data = json.loads(portfolio_data)
        else:
            portfolio_data = []
        
        # ایجاد شناسه منحصر به فرد برای موقعیت
        position_id = f"main_{int(time.time() * 1000)}_{random.randint(1000, 9999)}"
        
        # اضافه کردن موقعیت جدید به Main
        new_position = {
            'id': position_id,
            'symbol': symbol,
            'position': position,
            'entry_price': entry_price,
            'quantity': quantity,
            'added_at': datetime.now().isoformat()
        }
        
        portfolio_data.append(new_position)
        
        # ذخیره پورتفوی Main
        cursor.execute('''
            INSERT INTO user_portfolios (user_id, portfolio_name, portfolio_data) 
            VALUES (%s, 'Main', %s) 
            ON DUPLICATE KEY UPDATE portfolio_data = %s, updated_at = %s
        ''', (user_id, json.dumps(portfolio_data), json.dumps(portfolio_data), datetime.now()))
        
        conn.commit()
        
        logger.info(f"Position {symbol} ({position}) added to Main portfolio for user {user_id}")
        return jsonify({
            'success': True, 
            'message': 'موقعیت به پورتفوی Main اضافه شد',
            'position': new_position,
            'position_id': position_id
        })
        
    except Exception as e:
        logger.error(f"Error in add_to_main_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/portfolio/<int:user_id>/remove_from_main', methods=['POST'])
def remove_from_main_portfolio(user_id):
    """حذف موقعیت از پورتفوی Main"""
    conn = None
    cursor = None
    try:
        data = request.json
        symbol = data.get('symbol')
        
        if not symbol:
            return jsonify({'success': False, 'error': 'نماد الزامی است'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'خطا در اتصال به پایگاه داده'})
        
        cursor = conn.cursor()
        
        # دریافت پورتفوی Main فعلی
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = 'Main'
        """, (user_id,))
        
        result = cursor.fetchone()
        
        if not result:
            return jsonify({'success': False, 'error': 'پورتفوی Main یافت نشد'})
        
        portfolio_data = result[0]
        if isinstance(portfolio_data, str):
            portfolio_data = json.loads(portfolio_data)
        
        # حذف تمام موقعیت‌های مربوط به این نماد از Main
        original_count = len(portfolio_data)
        portfolio_data = [item for item in portfolio_data if item.get('symbol') != symbol]
        new_count = len(portfolio_data)
        removed_count = original_count - new_count
        
        if removed_count == 0:
            return jsonify({'success': False, 'error': 'موقعیتی با این نماد در Main یافت نشد'})
        
        # ذخیره پورتفوی Main به‌روزشده
        cursor.execute('''
            INSERT INTO user_portfolios (user_id, portfolio_name, portfolio_data) 
            VALUES (%s, 'Main', %s) 
            ON DUPLICATE KEY UPDATE portfolio_data = %s, updated_at = %s
        ''', (user_id, json.dumps(portfolio_data), json.dumps(portfolio_data), datetime.now()))
        
        conn.commit()
        
        logger.info(f"Removed {removed_count} position(s) of {symbol} from Main portfolio for user {user_id}")
        return jsonify({
            'success': True, 
            'message': f'{removed_count} موقعیت {symbol} از پورتفوی Main حذف شد',
            'removed_count': removed_count,
            'remaining_count': new_count
        })
        
    except Exception as e:
        logger.error(f"Error in remove_from_main_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()
                
@app.route('/api/user/portfolios/<int:user_id>', methods=['GET'])
def get_user_portfolios(user_id):
    """دریافت لیست تمام پورتفوهای کاربر با تعداد موقعیت‌ها"""
    conn = None
    cursor = None
    try:
        logger.info(f"Getting portfolios for user {user_id}")
        
        conn = get_db_connection()
        if not conn:
            logger.error("Database connection failed")
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor(dictionary=True)
        
        cursor.execute("""
            SELECT 
                portfolio_name, 
                updated_at,
                JSON_LENGTH(portfolio_data) as position_count
            FROM user_portfolios 
            WHERE user_id = %s 
            ORDER BY 
                CASE WHEN portfolio_name = 'Main' THEN 0 ELSE 1 END,
                updated_at DESC
        """, (user_id,))
        
        portfolios = cursor.fetchall()
        
        # تبدیل position_count به عدد
        for portfolio in portfolios:
            if portfolio['position_count'] is None:
                portfolio['position_count'] = 0
            else:
                portfolio['position_count'] = int(portfolio['position_count'])
        
        logger.info(f"Found {len(portfolios)} portfolios for user {user_id}: {[p['portfolio_name'] for p in portfolios]}")
        
        return jsonify({
            'success': True,
            'portfolios': portfolios
        })
        
    except Exception as e:
        logger.error(f"Error in get_user_portfolios: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/device/portfolio/<username>', methods=['GET'])
def get_device_portfolio(username):
    """دریافت اطلاعات پورتفو برای دستگاه ESP32 با سورت کردن براساس ضرر"""
    conn = None
    cursor = None
    try:
        portfolio_name = request.args.get('portfolio_name', 'Main')
        limit = int(request.args.get('limit', 70))  # دریافت تعداد موقعیت‌ها (پیش‌فرض: 70)
        
        # Authentication (کد قبلی)
        auth_header = request.headers.get('Authorization')
        if not auth_header or not auth_header.startswith('Basic '):
            return jsonify({'success': False, 'error': 'Authentication required'})
        
        auth_decoded = base64.b64decode(auth_header[6:]).decode('utf-8')
        auth_username, auth_password = auth_decoded.split(':', 1)
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor(dictionary=True)
        
        # Verify user
        cursor.execute("SELECT id, username, password_hash FROM users WHERE username = %s", (auth_username,))
        user = cursor.fetchone()
        
        if not user:
            return jsonify({'success': False, 'error': 'User not found'})
        
        if user['password_hash'] != hash_password(auth_password):
            return jsonify({'success': False, 'error': 'Invalid password'})
        
        if user['username'] != username:
            return jsonify({'success': False, 'error': 'Access denied'})
        
        user_id = user['id']
        
        # Get portfolio
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = %s
        """, (user_id, portfolio_name))
        
        result = cursor.fetchone()
        
        if not result:
            return jsonify({
                'success': True,
                'portfolio': [],
                'summary': {
                    'total_investment': 0,
                    'total_current_value': 0,
                    'total_pnl': 0,
                    'total_pnl_percent': 0,
                    'total_positions': 0,
                    'valid_positions': 0,
                    'long_positions': 0,
                    'short_positions': 0
                },
                'current_prices': {}
            })
        
        portfolio_data = result['portfolio_data']
        if isinstance(portfolio_data, str):
            try:
                portfolio_data = json.loads(portfolio_data)
            except json.JSONDecodeError as e:
                logger.error(f"JSON decode error: {e}")
                portfolio_data = []
        
        if not isinstance(portfolio_data, list):
            logger.error(f"Portfolio data is not a list: {type(portfolio_data)}")
            portfolio_data = []
        
        # تحلیل پورتفو
        total_investment = 0
        total_current_value = 0
        total_pnl = 0
        valid_positions = 0
        long_positions = 0
        short_positions = 0
        current_prices = {}
        all_positions = []  # لیست تمام موقعیت‌ها با اطلاعات کامل
        
        logger.info(f"=== DEBUG: Processing portfolio for {username} ===")
        logger.info(f"Total items in portfolio: {len(portfolio_data)}")
        
        for index, item in enumerate(portfolio_data):
            try:
                if not isinstance(item, dict):
                    logger.warning(f"Skipping non-dict item at index {index}: {item}")
                    continue
                    
                symbol = item.get('symbol')
                position = item.get('position', '').lower().strip()  # Normalize position
                entry_price_str = item.get('entry_price')
                quantity_str = item.get('quantity', '1.0')
                
                if not symbol:
                    logger.warning(f"Skipping item at index {index} without symbol")
                    continue
                
                if not position:
                    logger.warning(f"Item {symbol} has no position, defaulting to 'long'")
                    position = 'long'
                
                # Validate position type
                if position not in ['long', 'short']:
                    logger.error(f"Invalid position type for {symbol}: '{position}'. Must be 'long' or 'short'. Defaulting to 'long'")
                    position = 'long'
                
                # Convert values
                try:
                    entry_price = float(entry_price_str) if entry_price_str else 0
                    quantity = float(quantity_str) if quantity_str else 1.0
                except (ValueError, TypeError) as e:
                    logger.error(f"Error converting values for {symbol}: {e}")
                    continue
                
                if entry_price <= 0:
                    logger.warning(f"Skipping {symbol} - invalid entry price: {entry_price}")
                    continue
                
                # Get current price
                cursor.execute("""
                    SELECT price FROM futures_prices 
                    WHERE symbol = %s 
                    ORDER BY timestamp DESC 
                    LIMIT 1
                """, (symbol,))
                price_result = cursor.fetchone()
                
                if not price_result or price_result['price'] is None:
                    logger.warning(f"No price found for {symbol}")
                    continue
                
                try:
                    current_price = float(price_result['price'])
                    
                    # محاسبه سرمایه و ارزش فعلی
                    investment = entry_price * quantity
                    current_value = current_price * quantity
                    
                    # محاسبه P&L بر اساس نوع موقعیت
                    if position == 'long':
                        pnl = (current_price - entry_price) * quantity
                        long_positions += 1
                        is_long_for_json = True
                        position_for_json = 'long'
                    elif position == 'short':
                        pnl = (entry_price - current_price) * quantity
                        short_positions += 1
                        is_long_for_json = False
                        position_for_json = 'short'
                    else:
                        logger.error(f"Unexpected position type for {symbol}: {position}")
                        pnl = 0
                        is_long_for_json = True
                        position_for_json = 'long'
                    
                    # محاسبه درصد P&L
                    pnl_percent = (pnl / investment * 100) if investment > 0 else 0
                    
                    # ذخیره موقعیت
                    position_info = {
                        'symbol': symbol,
                        'position': position_for_json,
                        'entry_price': round(entry_price, 8),
                        'current_price': round(current_price, 8),
                        'quantity': round(quantity, 8),
                        'investment': round(investment, 2),
                        'current_value': round(current_value, 2),
                        'pnl': round(pnl, 2),
                        'pnl_percent': round(pnl_percent, 2),
                        'is_profitable': pnl >= 0,
                        'is_long': is_long_for_json,
                        'position_side': position_for_json.upper(),
                        'margin_type': 'ISOLATED',
                        'raw_pnl_percent': pnl_percent  # برای سورت کردن
                    }
                    
                    all_positions.append(position_info)
                    
                    # جمع مقادیر کل
                    total_investment += investment
                    total_current_value += current_value
                    total_pnl += pnl
                    valid_positions += 1
                    
                    # ذخیره قیمت کنونی
                    current_prices[symbol] = current_price
                    
                    logger.debug(f"Processed {symbol}: {position}, Entry=${entry_price:.4f}, Current=${current_price:.4f}, "
                               f"Qty={quantity:.4f}, P&L=${pnl:.2f} ({pnl_percent:.2f}%)")
                    
                except Exception as e:
                    logger.error(f"Error processing {symbol}: {e}")
                    continue
                    
            except Exception as e:
                logger.error(f"Error in portfolio item {index}: {e}")
                continue
        
        # سورت کردن موقعیت‌ها بر اساس pnl_percent (از منفی‌ترین به مثبت)
        all_positions.sort(key=lambda x: x['raw_pnl_percent'])
        
        # محدود کردن به 70 موقعیت اول (منفی‌ترین‌ها)
        limited_positions = all_positions[:limit]
        
        # محاسبه کل درصد P&L
        if total_investment > 0:
            total_pnl_percent = ((total_current_value - total_investment) / total_investment * 100)
        else:
            total_pnl_percent = 0
        
        # اعتبارسنجی: بررسی تناقض در P&L
        calculated_pnl = total_current_value - total_investment
        if abs(total_pnl - calculated_pnl) > 0.01:
            logger.warning(f"P&L mismatch! Sum of positions=${total_pnl:.2f}, Calculated=${calculated_pnl:.2f}")
            total_pnl = calculated_pnl
        
        # ساخت پاسخ JSON
        response_data = {
            'success': True,
            'portfolio': limited_positions,  # فقط موقعیت‌های سورت شده و محدود شده
            'portfolio_summary': {
                'total_investment': round(total_investment, 2),
                'total_current_value': round(total_current_value, 2),
                'total_pnl': round(total_pnl, 2),
                'total_pnl_percent': round(total_pnl_percent, 2),
                'total_positions': len(portfolio_data),
                'long_positions': long_positions,
                'short_positions': short_positions,
                'sorted_positions': len(limited_positions),  # تعداد موقعیت‌های سورت شده
                'sort_type': 'by_loss'  # نشان می‌دهد داده‌ها سورت شده‌اند
            },
            'summary': {
                'timestamp': datetime.now().isoformat(),
                'total_investment': round(total_investment, 2),
                'total_current_value': round(total_current_value, 2),
                'total_pnl': round(total_pnl, 2),
                'total_pnl_percent': round(total_pnl_percent, 2),
                'total_positions': len(portfolio_data),
                'valid_positions': valid_positions,
                'sorted_count': len(limited_positions)
            },
            'current_prices': current_prices
        }
        
        logger.info(f"=== DEBUG: Response Data Summary ===")
        logger.info(f"Original positions: {len(all_positions)}")
        logger.info(f"Sorted positions sent: {len(limited_positions)}")
        logger.info(f"Sorting: by pnl_percent (lowest first)")
        logger.info(f"Final P&L in response: ${response_data['summary']['total_pnl']:.2f}")
        logger.info(f"Final P&L% in response: {response_data['summary']['total_pnl_percent']:.2f}%")
        
        return jsonify(response_data)
        
    except Exception as e:
        logger.error(f"Error in get_device_portfolio: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()
                                                
# تابع مشترک برای ایجاد پورتفوی اتوماتیک با افست زمانی - با قیمت تاریخی
def create_auto_portfolio_with_offset(minutes_offset=0):
    """تابع مشترک برای ایجاد پورتفوی اتوماتیک با افست زمانی و قیمت تاریخی"""
    conn = None
    cursor = None
    try:
        data = request.json
        user_id = data.get('user_id')
        investment = float(data.get('investment', 10))
        positions_per_type = int(data.get('positions_per_type', 25))
        
        # نام پورتفو بر اساس افست زمانی
        now = datetime.now()
        if minutes_offset == 0:
            portfolio_name = data.get('portfolio_name', f'auto_{now.strftime("%Y%m%d_%H%M%S")}')
        elif minutes_offset == 30:
            portfolio_name = data.get('portfolio_name', f'auto_30min_{now.strftime("%Y%m%d_%H%M%S")}')
        elif minutes_offset == 60:
            portfolio_name = data.get('portfolio_name', f'auto_60min_{now.strftime("%Y%m%d_%H%M%S")}')
        else:
            portfolio_name = data.get('portfolio_name', f'auto_{minutes_offset}min_{now.strftime("%Y%m%d_%H%M%S")}')
        
        # اعتبارسنجی
        if not user_id:
            return jsonify({'success': False, 'error': 'شناسه کاربر الزامی است'})
        
        if investment <= 0:
            return jsonify({'success': False, 'error': 'سرمایه باید بزرگتر از صفر باشد'})
        
        if positions_per_type < 5 or positions_per_type > 100:
            return jsonify({'success': False, 'error': 'تعداد موقعیت‌ها باید بین 5 تا 100 باشد'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'خطا در اتصال به پایگاه داده'})
        
        cursor = conn.cursor(dictionary=True)
        
        # شروع تراکنش
        cursor.execute("START TRANSACTION")
        
        try:
            # 1. پاک کردن جدول موقت
            cursor.execute("DELETE FROM `futures_prices_4h` WHERE 1")
            
            # 2. وارد کردن داده‌های 4 ساعت منتهی به X دقیقه قبل
            if minutes_offset == 0:
                # داده‌های 4 ساعت اخیر (بدون افست)
                insert_query = """
                    INSERT INTO `futures_prices_4h` (
                        `symbol`, `price`, `timestamp`, `previous_price`, `price_diff`, `price_diff_pct`
                    ) 
                    SELECT 
                        `symbol`, `price`, `timestamp`, `base_price`, `price_change`, `price_change_percent` 
                    FROM `futures_prices` 
                    WHERE `timestamp` > NOW() - INTERVAL 4 HOUR
                """
            else:
                # داده‌های 4 ساعت منتهی به X دقیقه قبل
                from_time = f"NOW() - INTERVAL 4 HOUR - INTERVAL {minutes_offset} MINUTE"
                to_time = f"NOW() - INTERVAL {minutes_offset} MINUTE"
                
                insert_query = f"""
                    INSERT INTO `futures_prices_4h` (
                        `symbol`, `price`, `timestamp`, `previous_price`, `price_diff`, `price_diff_pct`
                    ) 
                    SELECT 
                        `symbol`, `price`, `timestamp`, `base_price`, `price_change`, `price_change_percent` 
                    FROM `futures_prices` 
                    WHERE `timestamp` BETWEEN {from_time} AND {to_time}
                """
            
            cursor.execute(insert_query)
            
            inserted_count = cursor.rowcount
            logger.info(f"Inserted {inserted_count} records for {minutes_offset} minutes offset")
            
            # 3. کوئری نزولی (برای short) - با قیمت تاریخی دقیق
            query_asc = f"""
                SELECT `fp4h`.`symbol` AS `symbol`, 
                       sum(`fp4h`.`price_diff_pct`) AS `Sum_price_diff_pct`, 
                       CASE 
                           WHEN {minutes_offset} = 0 THEN 
                               (SELECT `futures_prices`.`price` 
                                FROM `futures_prices` 
                                WHERE `futures_prices`.`symbol` = `fp4h`.`symbol` 
                                ORDER BY `futures_prices`.`timestamp` DESC 
                                LIMIT 1)
                           ELSE 
                               (SELECT `fp_hist`.`price` 
                                FROM `futures_prices` AS `fp_hist`
                                WHERE `fp_hist`.`symbol` = `fp4h`.`symbol` 
                                  AND `fp_hist`.`timestamp` <= DATE_SUB(NOW(), INTERVAL {minutes_offset} MINUTE)
                                ORDER BY ABS(TIMESTAMPDIFF(SECOND, `fp_hist`.`timestamp`, DATE_SUB(NOW(), INTERVAL {minutes_offset} MINUTE))) ASC
                                LIMIT 1)
                       END AS `historical_price`,
                       CASE WHEN exists(select 1 from `kcex_futures` 
                                       where `kcex_futures`.`symbol` = `fp4h`.`symbol` limit 1) 
                            THEN 'Yes' ELSE 'No' END AS `in_kcex` 
                FROM `futures_prices_4h` AS `fp4h` 
                GROUP BY `fp4h`.`symbol` 
                ORDER BY sum(`fp4h`.`price_diff_pct`) ASC
                LIMIT {positions_per_type * 2}
            """
            
            cursor.execute(query_asc)
            ascending_results = cursor.fetchall()
            
            # 4. کوئری صعودی (برای long) - با قیمت تاریخی دقیق
            query_desc = f"""
                SELECT `fp4h`.`symbol` AS `symbol`, 
                       sum(`fp4h`.`price_diff_pct`) AS `Sum_price_diff_pct`, 
                       CASE 
                           WHEN {minutes_offset} = 0 THEN 
                               (SELECT `futures_prices`.`price` 
                                FROM `futures_prices` 
                                WHERE `futures_prices`.`symbol` = `fp4h`.`symbol` 
                                ORDER BY `futures_prices`.`timestamp` DESC 
                                LIMIT 1)
                           ELSE 
                               (SELECT `fp_hist`.`price` 
                                FROM `futures_prices` AS `fp_hist`
                                WHERE `fp_hist`.`symbol` = `fp4h`.`symbol` 
                                  AND `fp_hist`.`timestamp` <= DATE_SUB(NOW(), INTERVAL {minutes_offset} MINUTE)
                                ORDER BY ABS(TIMESTAMPDIFF(SECOND, `fp_hist`.`timestamp`, DATE_SUB(NOW(), INTERVAL {minutes_offset} MINUTE))) ASC
                                LIMIT 1)
                       END AS `historical_price`,
                       CASE WHEN exists(select 1 from `kcex_futures` 
                                       where `kcex_futures`.`symbol` = `fp4h`.`symbol` limit 1) 
                            THEN 'Yes' ELSE 'No' END AS `in_kcex` 
                FROM `futures_prices_4h` AS `fp4h` 
                GROUP BY `fp4h`.`symbol` 
                ORDER BY sum(`fp4h`.`price_diff_pct`) DESC
                LIMIT {positions_per_type * 2}
            """
            
            cursor.execute(query_desc)
            descending_results = cursor.fetchall()
            
            # 5. دریافت قیمت کنونی برای همه نمادها (برای نمایش در نتایج و تحلیل PNL)
            current_prices = {}
            all_symbols = set([row['symbol'] for row in ascending_results] + [row['symbol'] for row in descending_results])
            
            for symbol in all_symbols:
                cursor.execute("""
                    SELECT price FROM futures_prices 
                    WHERE symbol = %s 
                    ORDER BY timestamp DESC 
                    LIMIT 1
                """, (symbol,))
                price_result = cursor.fetchone()
                if price_result and price_result['price']:
                    current_prices[symbol] = float(price_result['price'])
            
            # 6. پردازش نتایج و ایجاد موقعیت‌ها با قیمت تاریخی
            current_time = datetime.now()
            all_positions = []
            total_historical_investment = 0
            total_current_value = 0
            total_pnl = 0
            
            # پردازش نتایج نزولی برای short
            short_count = 0
            for row in ascending_results:
                if short_count >= positions_per_type:
                    break
                    
                symbol = row['symbol']
                
                # استفاده از قیمت تاریخی برای ورود
                historical_price = float(row['historical_price']) if row['historical_price'] else 0
                
                if historical_price <= 0:
                    # اگر قیمت تاریخی یافت نشد، از لیست پرش کن
                    logger.warning(f"No historical price found for {symbol} at {minutes_offset} minutes ago, skipping...")
                    continue
                
                in_kcex = row['in_kcex'] == 'Yes'
                
                quantity = investment / historical_price if historical_price > 0 else 0
                
                # قیمت کنونی برای نمایش در نتایج و محاسبه PNL
                current_price = current_prices.get(symbol, historical_price)
                
                # محاسبه سرمایه و PNL
                investment_amount = investment
                current_value = current_price * quantity
                position_pnl = (historical_price - current_price) * quantity  # برای شورت: اگر قیمت افت کند سود می‌دهد
                
                total_historical_investment += investment_amount
                total_current_value += current_value
                total_pnl += position_pnl
                
                position_data = {
                    "id": f"auto_{minutes_offset}min_{symbol}_{current_time.strftime('%Y%m%d%H%M%S')}",
                    "symbol": symbol,
                    "position": "short",
                    "entry_price": historical_price,  # قیمت تاریخی برای ورود
                    "current_price": current_price,   # قیمت کنونی برای نمایش
                    "quantity": round(quantity, 8),
                    "investment": round(investment_amount, 2),
                    "added_at": current_time.strftime("%Y-%m-%dT%H:%M:%S.000Z"),
                    "price_diff_pct": float(row['Sum_price_diff_pct']) if row['Sum_price_diff_pct'] else 0,
                    "in_kcex": in_kcex,
                    "time_offset": f"{minutes_offset}min",
                    "is_historical": minutes_offset > 0,
                    "historical_timestamp": f"{minutes_offset} minutes ago",
                    "pnl": round(position_pnl, 2),
                    "pnl_percent": round((position_pnl / investment_amount * 100) if investment_amount > 0 else 0, 2)
                }
                
                all_positions.append(position_data)
                short_count += 1
            
            # پردازش نتایج صعودی برای long
            long_count = 0
            for row in descending_results:
                if long_count >= positions_per_type:
                    break
                    
                symbol = row['symbol']
                
                # استفاده از قیمت تاریخی برای ورود
                historical_price = float(row['historical_price']) if row['historical_price'] else 0
                
                if historical_price <= 0:
                    # اگر قیمت تاریخی یافت نشد، از لیست پرش کن
                    logger.warning(f"No historical price found for {symbol} at {minutes_offset} minutes ago, skipping...")
                    continue
                
                in_kcex = row['in_kcex'] == 'Yes'
                
                quantity = investment / historical_price if historical_price > 0 else 0
                
                # قیمت کنونی برای نمایش در نتایج و محاسبه PNL
                current_price = current_prices.get(symbol, historical_price)
                
                # محاسبه سرمایه و PNL
                investment_amount = investment
                current_value = current_price * quantity
                position_pnl = (current_price - historical_price) * quantity  # برای لانگ: اگر قیمت رشد کند سود می‌دهد
                
                total_historical_investment += investment_amount
                total_current_value += current_value
                total_pnl += position_pnl
                
                position_data = {
                    "id": f"auto_{minutes_offset}min_{symbol}_{current_time.strftime('%Y%m%d%H%M%S')}",
                    "symbol": symbol,
                    "position": "long",
                    "entry_price": historical_price,  # قیمت تاریخی برای ورود
                    "current_price": current_price,   # قیمت کنونی برای نمایش
                    "quantity": round(quantity, 8),
                    "investment": round(investment_amount, 2),
                    "added_at": current_time.strftime("%Y-%m-%dT%H:%M:%S.000Z"),
                    "price_diff_pct": float(row['Sum_price_diff_pct']) if row['Sum_price_diff_pct'] else 0,
                    "in_kcex": in_kcex,
                    "time_offset": f"{minutes_offset}min",
                    "is_historical": minutes_offset > 0,
                    "historical_timestamp": f"{minutes_offset} minutes ago",
                    "pnl": round(position_pnl, 2),
                    "pnl_percent": round((position_pnl / investment_amount * 100) if investment_amount > 0 else 0, 2)
                }
                
                all_positions.append(position_data)
                long_count += 1
            
            # 7. ذخیره پورتفو
            if all_positions:
                portfolio_json = json.dumps(all_positions)
                
                cursor.execute('''
                    INSERT INTO user_portfolios (user_id, portfolio_name, portfolio_data, created_at, updated_at)
                    VALUES (%s, %s, %s, %s, %s)
                    ON DUPLICATE KEY UPDATE portfolio_data = %s, updated_at = %s
                ''', (user_id, portfolio_name, portfolio_json, current_time, current_time, portfolio_json, current_time))
            
            # کامیت تراکنش
            conn.commit()
            
            logger.info(f"Auto portfolio created with {minutes_offset}min offset for user {user_id}: {len(all_positions)} positions")
            
            # محاسبه PNL کل
            total_pnl_percent = (total_pnl / total_historical_investment * 100) if total_historical_investment > 0 else 0
            
            # پیام مناسب بر اساس افست زمانی
            if minutes_offset == 0:
                time_message = 'داده‌های 4 ساعت اخیر (قیمت کنونی)'
                price_type = 'قیمت کنونی'
            else:
                time_message = f'داده‌های 4 ساعت منتهی به {minutes_offset} دقیقه قبل (قیمت {minutes_offset} دقیقه قبل)'
                price_type = f'قیمت {minutes_offset} دقیقه قبل'
            
            return jsonify({
                'success': True,
                'message': f'پورتفوی اتوماتیک ({time_message}) با موفقیت ایجاد شد',
                'total_positions': len(all_positions),
                'long_count': long_count,
                'short_count': short_count,
                'kcex_count': len([p for p in all_positions if p['in_kcex']]),
                'total_investment': round(total_historical_investment, 2),
                'total_current_value': round(total_current_value, 2),
                'total_pnl': round(total_pnl, 2),
                'total_pnl_percent': round(total_pnl_percent, 2),
                'portfolio_name': portfolio_name,
                'time_offset': f'{minutes_offset} دقیقه قبل' if minutes_offset > 0 else 'داده‌های 4 ساعت اخیر',
                'data_period': time_message,
                'records_processed': inserted_count,
                'is_historical': minutes_offset > 0,
                'price_type': price_type,
                'historical_minutes': minutes_offset
            })
            
        except Exception as e:
            # رول‌بک در صورت خطا
            conn.rollback()
            logger.error(f"Error creating auto portfolio with {minutes_offset}min offset: {e}")
            return jsonify({'success': False, 'error': f'خطا در ایجاد پورتفوی اتوماتیک: {str(e)}'})
        
    except Exception as e:
        logger.error(f"Error in create_auto_portfolio_with_offset: {e}")
        return jsonify({'success': False, 'error': 'خطای سرور'})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

@app.route('/api/auto-portfolio/create', methods=['POST'])
def create_auto_portfolio():
    """ایجاد پورتفوی اتوماتیک بر اساس الگوریتم پیشرفته (داده‌های 4 ساعت اخیر با قیمت کنونی)"""
    return create_auto_portfolio_with_offset(minutes_offset=0)

@app.route('/api/auto-portfolio/create_30min', methods=['POST'])
def create_auto_portfolio_30min():
    """ایجاد پورتفوی اتوماتیک بر اساس داده‌های 4 ساعت منتهی به 30 دقیقه قبل با قیمت 30 دقیقه قبل"""
    return create_auto_portfolio_with_offset(minutes_offset=30)

@app.route('/api/auto-portfolio/create_60min', methods=['POST'])
def create_auto_portfolio_60min():
    """ایجاد پورتفوی اتوماتیک بر اساس داده‌های 4 ساعت منتهی به 60 دقیقه قبل با قیمت 60 دقیقه قبل"""
    return create_auto_portfolio_with_offset(minutes_offset=60)

@app.route('/api/portfolio/copy_kcex_to_arduino/<int:user_id>', methods=['POST'])
def copy_kcex_to_arduino_portfolio(user_id):
    """کپی پورتفو به Arduino با امکان انتخاب نوع کپی (فقط KCEX یا همه رمزارزها)"""
    conn = None
    cursor = None
    try:
        data = request.json
        source_portfolio_name = data.get('source_portfolio')
        copy_type = data.get('copy_type', 'kcex_only')  # مقدار پیش‌فرض: فقط KCEX
        
        # اعتبارسنجی پارامترها
        if not source_portfolio_name:
            return jsonify({'success': False, 'error': 'نام پورتفوی مبدا الزامی است'})
        
        if source_portfolio_name == 'Arduino':
            return jsonify({'success': False, 'error': 'نمی‌توان پورتفوی Arduino را به خودش کپی کرد'})
        
        if copy_type not in ['kcex_only', 'all_symbols']:
            return jsonify({'success': False, 'error': 'نوع کپی نامعتبر است. مقادیر مجاز: kcex_only یا all_symbols'})
        
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'خطا در اتصال به پایگاه داده'})
        
        cursor = conn.cursor(dictionary=True)
        
        # دریافت پورتفوی مبدا
        cursor.execute("""
            SELECT portfolio_data FROM user_portfolios 
            WHERE user_id = %s AND portfolio_name = %s
        """, (user_id, source_portfolio_name))
        
        result = cursor.fetchone()
        
        if not result:
            return jsonify({'success': False, 'error': f'پورتفوی "{source_portfolio_name}" یافت نشد'})
        
        portfolio_data = result['portfolio_data']
        if isinstance(portfolio_data, str):
            portfolio_data = json.loads(portfolio_data)
        
        if not portfolio_data or len(portfolio_data) == 0:
            return jsonify({'success': False, 'error': 'پورتفوی مبدا خالی است'})
        
        # فیلتر کردن بر اساس نوع کپی
        filtered_positions = []
        stats = {
            'total_positions': len(portfolio_data),
            'kcex_count': 0,
            'non_kcex_count': 0,
            'filtered_out': 0,
            'copied_positions': 0
        }
        
        for item in portfolio_data:
            if not item or 'symbol' not in item:
                continue
                
            symbol = item['symbol']
            
            # بررسی اینکه آیا این نماد در KCEX موجود است
            cursor.execute("SELECT COUNT(*) as count FROM kcex_futures WHERE symbol = %s", (symbol,))
            kcex_result = cursor.fetchone()
            in_kcex = kcex_result['count'] > 0 if kcex_result else False
            
            # اعمال فیلتر بر اساس نوع کپی
            should_copy = False
            
            if copy_type == 'kcex_only':
                # فقط نمادهای KCEX
                if in_kcex:
                    should_copy = True
                    stats['kcex_count'] += 1
                else:
                    stats['filtered_out'] += 1
            else:  # copy_type == 'all_symbols'
                # همه نمادها
                should_copy = True
                if in_kcex:
                    stats['kcex_count'] += 1
                else:
                    stats['non_kcex_count'] += 1
            
            if should_copy:
                # اضافه کردن شناسه و زمان به موقعیت فیلتر شده
                current_time = datetime.now().isoformat()
                new_item = item.copy()
                
                if 'id' not in new_item:
                    new_item['id'] = f"arduino_{copy_type}_{int(time.time() * 1000)}_{random.randint(1000, 9999)}"
                if 'copied_at' not in new_item:
                    new_item['copied_at'] = current_time
                if 'source_portfolio' not in new_item:
                    new_item['source_portfolio'] = source_portfolio_name
                if 'copy_type' not in new_item:
                    new_item['copy_type'] = copy_type
                if 'filtered_for_kcex' not in new_item:
                    new_item['filtered_for_kcex'] = (copy_type == 'kcex_only')
                
                filtered_positions.append(new_item)
                stats['copied_positions'] += 1
        
        # بررسی شرایط خاص
        if copy_type == 'kcex_only' and stats['copied_positions'] == 0:
            return jsonify({
                'success': False, 
                'error': f'هیچ موقعیتی از پورتفوی "{source_portfolio_name}" در KCEX موجود نیست. برای کپی همه موقعیت‌ها، گزینه "همه رمزارزها" را انتخاب کنید.',
                'copy_type': copy_type,
                'total_positions': stats['total_positions'],
                'suggestion': 'try_all_symbols'
            })
        
        if stats['copied_positions'] == 0:
            return jsonify({
                'success': False, 
                'error': f'هیچ موقعیتی برای کپی کردن پیدا نشد',
                'copy_type': copy_type,
                'total_positions': stats['total_positions']
            })
        
        # ذخیره در پورتفوی Arduino
        cursor.execute('''
            INSERT INTO user_portfolios (user_id, portfolio_name, portfolio_data, created_at, updated_at)
            VALUES (%s, 'Arduino', %s, %s, %s)
            ON DUPLICATE KEY UPDATE portfolio_data = %s, updated_at = %s
        ''', (user_id, json.dumps(filtered_positions), datetime.now(), datetime.now(), 
              json.dumps(filtered_positions), datetime.now()))
        
        conn.commit()
        
        # لاگ کردن عملیات
        if copy_type == 'kcex_only':
            logger.info(f"Copied {stats['copied_positions']} KCEX positions (filtered out {stats['filtered_out']}) from '{source_portfolio_name}' to 'Arduino' for user {user_id}")
        else:
            logger.info(f"Copied {stats['copied_positions']} total positions ({stats['kcex_count']} KCEX, {stats['non_kcex_count']} non-KCEX) from '{source_portfolio_name}' to 'Arduino' for user {user_id}")
        
        # بازگرداندن نتیجه
        response_data = {
            'success': True,
            'message': f'پورتفوی "{source_portfolio_name}" با موفقیت به پورتفوی Arduino کپی شد',
            'copy_type': copy_type,
            'copied_positions': stats['copied_positions'],
            'total_positions': stats['total_positions'],
            'kcex_count': stats['kcex_count'],
            'non_kcex_count': stats['non_kcex_count'],
            'filtered_out': stats['filtered_out'],
            'source_portfolio': source_portfolio_name,
            'destination_portfolio': 'Arduino',
            'kcex_only': (copy_type == 'kcex_only')
        }
        
        if copy_type == 'kcex_only':
            response_data['message'] = f'پورتفوی "{source_portfolio_name}" (فقط رمزارزهای KCEX) با موفقیت به پورتفوی Arduino کپی شد'
        else:
            response_data['message'] = f'پورتفوی "{source_portfolio_name}" (همه رمزارزها) با موفقیت به پورتفوی Arduino کپی شد'
        
        return jsonify(response_data)
        
    except Exception as e:
        logger.error(f"Error copying to Arduino portfolio (type: {copy_type}): {e}")
        return jsonify({'success': False, 'error': f'خطا در کپی کردن پورتفو: {str(e)}'})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()
            
# API برای بررسی وضعیت KCEX یک symbol خاص
@app.route('/api/kcex/check/<symbol>', methods=['GET'])
def check_kcex_status(symbol):
    """بررسی وضعیت KCEX برای یک symbol خاص"""
    conn = None
    cursor = None
    try:
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor()
        
        cursor.execute("SELECT COUNT(*) as count FROM kcex_futures WHERE symbol = %s", (symbol,))
        result = cursor.fetchone()
        in_kcex = result[0] > 0 if result else False
        
        return jsonify({
            'success': True,
            'symbol': symbol,
            'in_kcex': in_kcex
        })
        
    except Exception as e:
        logger.error(f"Error checking KCEX status: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()

# API برای دریافت همه symbolهای KCEX
@app.route('/api/kcex/symbols', methods=['GET'])
def get_kcex_symbols():
    """دریافت تمام symbolهای موجود در KCEX"""
    conn = None
    cursor = None
    try:
        conn = get_db_connection()
        if not conn:
            return jsonify({'success': False, 'error': 'Database connection failed'})
        
        cursor = conn.cursor()
        
        cursor.execute("SELECT DISTINCT symbol FROM kcex_futures ORDER BY symbol")
        symbols = [row[0] for row in cursor.fetchall()]
        
        return jsonify({
            'success': True,
            'symbols': symbols,
            'count': len(symbols)
        })
        
    except Exception as e:
        logger.error(f"Error getting KCEX symbols: {e}")
        return jsonify({'success': False, 'error': str(e)})
    finally:
        if cursor:
            cursor.close()
        if conn:
            conn.close()
            
# سرویس فایل‌های استاتیک

@app.route('/')
def serve_index():
    return send_from_directory('.', 'complete_portfolio_manager.html')

@app.route('/<path:path>')
def serve_static(path):
    return send_from_directory('.', path)

if __name__ == '__main__':
    # راه‌اندازی اولیه دیتابیس
    if init_database():
        logger.info("Database initialized successfully")
    else:
        logger.error("Failed to initialize database")
    
    logger.info("Crypto Portfolio Service with User System Started")
    logger.info("Service running on http://0.0.0.0:8000")
    logger.info("Portfolio analysis with quantity support is active")
    
    try:
        app.run(host='0.0.0.0', port=8000, debug=False)
    except KeyboardInterrupt:
        logger.info("Service stopped by user")
    except Exception as e:
        logger.error(f"Service stopped with error: {e}")