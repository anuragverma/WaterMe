pipeline {
  agent {
    docker {
      image 'takigama/platformio:latest'
      args '-u 1000:1000'
    }
  }

  environment {
    PROJECT = env.JOB_NAME.tokenize('/')[1]       // anuragverma/WaterMe → WaterMe
    BRANCH = env.BRANCH_NAME ?: 'unknown'         // fallback if not populated
    DATE = sh(script: "date +%Y%m%d", returnStdout: true).trim()
    BUILD_NO = env.BUILD_NUMBER
    UPLOAD_DIR = "http://repository.fundebazi.com/repository/${PROJECT}/${BRANCH}/${DATE}_${BUILD_NO}/"
    BIN_FILE = "firmware.bin"
    BIN_PATH = ".pio/build/esp32dev/${BIN_FILE}"
    REPO_USER = credentials('repository-creds').username
    REPO_PASS = credentials('repository-creds').password
  }

  stages {
    stage('Checkout') {
      steps {
        checkout scm
      }
    }

    stage('Build Firmware') {
      steps {
        sh 'pio run'
      }
    }

    stage('Prepare Upload Directory') {
      steps {
        script {
          def levels = UPLOAD_DIR.replace('http://repository.fundebazi.com/', '').split('/')
          def path = ''
          for (int i = 0; i < levels.size(); i++) {
            path += '/' + levels[i]
            sh """
              curl -sf -u ${REPO_USER}:${REPO_PASS} -X MKCOL http://repository.fundebazi.com${path} || true
            """
          }
        }
      }
    }

    stage('Upload Firmware') {
      steps {
        sh """
          curl -u ${REPO_USER}:${REPO_PASS} \\
            -X PUT --upload-file ${BIN_PATH} \\
            ${UPLOAD_DIR}${BIN_FILE}
        """
      }
    }

    stage('Verify Upload') {
      steps {
        sh "curl -sI ${UPLOAD_DIR}${BIN_FILE}"
      }
    }
  }
}
